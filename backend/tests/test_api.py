import pytest
import pytest_asyncio
import os
from httpx import AsyncClient, ASGITransport
from sqlalchemy.ext.asyncio import create_async_engine, async_sessionmaker, AsyncSession

# Use in-memory SQLite for fast isolated testing
os.environ["DATABASE_URL"] = "sqlite+aiosqlite:///:memory:"
os.environ["ENVIRONMENT"] = "development"

from backend.app.main import app
from backend.app.database import Base, get_db, Device, MetricRecord, current_time_ms
from backend.app.auth import hash_key

TEST_DEVICE_ID = "edgeecg-test-01"
TEST_DEVICE_KEY = "super_secret_test_key_123"

test_engine = create_async_engine("sqlite+aiosqlite:///:memory:", echo=False)
TestingSessionLocal = async_sessionmaker(bind=test_engine, class_=AsyncSession, expire_on_commit=False)


async def override_get_db():
    async with TestingSessionLocal() as session:
        yield session


app.dependency_overrides[get_db] = override_get_db


@pytest_asyncio.fixture(autouse=True)
async def prepare_database():
    async with test_engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

    # Seed test device
    async with TestingSessionLocal() as db:
        dev = Device(
            id=TEST_DEVICE_ID,
            key_hash=hash_key(TEST_DEVICE_KEY),
            created_at=current_time_ms(),
            last_seen_at=None,
            active=True
        )
        db.add(dev)
        await db.commit()

    yield

    async with test_engine.begin() as conn:
        await conn.run_sync(Base.metadata.drop_all)


@pytest.mark.asyncio
async def test_health_check():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        resp = await client.get("/health")
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "healthy"
        assert "timestamp_ms" in data


@pytest.mark.asyncio
async def test_auth_rejection():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        # Missing header
        resp = await client.post("/api/v1/ingest", json={"schema_version": 1, "device_id": TEST_DEVICE_ID, "records": []})
        assert resp.status_code == 401

        # Invalid key
        resp = await client.post(
            "/api/v1/ingest",
            headers={"X-Device-Key": "wrong_key"},
            json={"schema_version": 1, "device_id": TEST_DEVICE_ID, "records": []}
        )
        assert resp.status_code == 401


@pytest.mark.asyncio
async def test_ingestion_and_idempotent_deduplication():
    payload = {
        "schema_version": 1,
        "device_id": TEST_DEVICE_ID,
        "records": [
            {
                "sequence": 1,
                "session_id": "session-1",
                "timestamp_ms": 1700000000000,
                "heart_rate_bpm": 72.0,
                "rmssd_ms": 42.0,
                "rr_interval_ms": 833.3,
                "signal_quality": "valid",
                "detected_peaks": 10,
                "rejected_peaks": 0
            },
            {
                "sequence": 2,
                "session_id": "session-1",
                "timestamp_ms": 1700000001000,
                "heart_rate_bpm": 74.0,
                "rmssd_ms": 41.5,
                "rr_interval_ms": 810.8,
                "signal_quality": "valid",
                "detected_peaks": 11,
                "rejected_peaks": 0
            }
        ]
    }

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        # 1. First submission: both should be accepted
        resp1 = await client.post(
            "/api/v1/ingest",
            headers={"X-Device-Key": TEST_DEVICE_KEY},
            json=payload
        )
        assert resp1.status_code == 200
        data1 = resp1.json()
        assert data1["accepted_sequences"] == [1, 2]
        assert data1["duplicate_sequences"] == []

        # 2. Replay submission 10 times (test idempotency NFR-005)
        for _ in range(10):
            resp_replay = await client.post(
                "/api/v1/ingest",
                headers={"X-Device-Key": TEST_DEVICE_KEY},
                json=payload
            )
            assert resp_replay.status_code == 200
            data_replay = resp_replay.json()
            assert data_replay["accepted_sequences"] == []
            assert data_replay["duplicate_sequences"] == [1, 2]

        # 3. Check database: exactly 2 rows exist, NO duplicates!
        async with TestingSessionLocal() as db:
            from sqlalchemy import select, func
            count = await db.scalar(select(func.count(MetricRecord.id)).where(MetricRecord.device_id == TEST_DEVICE_ID))
            assert count == 2


@pytest.mark.asyncio
async def test_pagination_and_cursor():
    # Insert 15 records with sequences 10..24
    async with TestingSessionLocal() as db:
        for seq in range(10, 25):
            rec = MetricRecord(
                device_id=TEST_DEVICE_ID,
                sequence=seq,
                session_id="session-p",
                timestamp_ms=1700000000000 + (seq * 1000),
                received_at=1700000000000 + (seq * 1000),
                heart_rate_bpm=65.0,
                rmssd_ms=30.0,
                rr_interval_ms=923.0,
                signal_quality="valid",
                detected_peaks=seq,
                rejected_peaks=0,
                schema_version=1
            )
            db.add(rec)
        await db.commit()

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        # Page 1: limit 10
        r1 = await client.get(f"/api/v1/devices/{TEST_DEVICE_ID}/records?limit=10")
        assert r1.status_code == 200
        p1 = r1.json()
        assert len(p1["records"]) == 10
        assert p1["records"][0]["sequence"] == 10
        assert p1["records"][-1]["sequence"] == 19
        assert p1["next_cursor"] == 19

        # Page 2: with cursor=19
        r2 = await client.get(f"/api/v1/devices/{TEST_DEVICE_ID}/records?limit=10&cursor={p1['next_cursor']}")
        assert r2.status_code == 200
        p2 = r2.json()
        assert len(p2["records"]) == 5
        assert p2["records"][0]["sequence"] == 20
        assert p2["records"][-1]["sequence"] == 24
        assert p2["next_cursor"] is None


@pytest.mark.asyncio
async def test_flags_and_latest_metric():
    async with TestingSessionLocal() as db:
        # Add high HR and sudden RR jump
        r1 = MetricRecord(
            device_id=TEST_DEVICE_ID,
            sequence=100,
            session_id="s1",
            timestamp_ms=1700000000000,
            received_at=1700000000000,
            heart_rate_bpm=135.0, # Tachycardia flag
            rmssd_ms=15.0,
            rr_interval_ms=444.0,
            signal_quality="valid",
            detected_peaks=50,
            rejected_peaks=0,
            schema_version=1
        )
        r2 = MetricRecord(
            device_id=TEST_DEVICE_ID,
            sequence=101,
            session_id="s1",
            timestamp_ms=1700000001000,
            received_at=1700000001000,
            heart_rate_bpm=60.0,
            rmssd_ms=50.0,
            rr_interval_ms=1000.0, # Sudden jump from 444 to 1000 (> 20%)
            signal_quality="valid",
            detected_peaks=51,
            rejected_peaks=0,
            schema_version=1
        )
        db.add_all([r1, r2])
        await db.commit()

    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
        # Check latest
        lat = await client.get(f"/api/v1/devices/{TEST_DEVICE_ID}/latest")
        assert lat.status_code == 200
        lat_data = lat.json()
        assert lat_data["device_id"] == TEST_DEVICE_ID
        assert lat_data["latest_record"]["sequence"] == 101
        assert lat_data["latest_record"]["heart_rate_bpm"] == 60.0

        # Check flags
        fl = await client.get(f"/api/v1/devices/{TEST_DEVICE_ID}/flags")
        assert fl.status_code == 200
        flags_data = fl.json()
        assert flags_data["total_flags"] >= 2
        types = [f["flag_type"] for f in flags_data["flags"]]
        assert "tachycardia_range" in types
        assert "rr_jump" in types

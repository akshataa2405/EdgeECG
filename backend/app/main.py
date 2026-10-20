import os
from contextlib import asynccontextmanager
from typing import Optional
from fastapi import FastAPI, Depends, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from sqlalchemy.ext.asyncio import AsyncSession

from .database import init_db, get_db, Device, current_time_ms
from .auth import get_current_device, verify_dashboard_auth, ensure_default_device
from .schemas import (
    IngestRequest, IngestResponse,
    LatestMetricResponse, PaginatedRecordsResponse,
    FlagsResponse, HealthResponse
)
from .ingestion import process_batch_ingestion
from .queries import fetch_latest_metric, fetch_historical_records, evaluate_device_flags


@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup: initialize database tables and provision default dev device
    await init_db()
    async for db in get_db():
        await ensure_default_device(db)
        break
    yield
    # Shutdown logic if needed


app = FastAPI(
    title="EdgeECG Backend API",
    version="1.0.0",
    description="Offline-tolerant ECG metric ingestion, deduplication, and historical query service.",
    lifespan=lifespan
)

# CORS configuration for browser dashboard access
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/health", response_model=HealthResponse, tags=["Diagnostics"])
async def health_check():
    return HealthResponse(
        status="healthy",
        timestamp_ms=current_time_ms(),
        database="connected"
    )


@app.post("/api/v1/ingest", response_model=IngestResponse, tags=["Ingestion"])
async def ingest_batch(
    payload: IngestRequest,
    device: Device = Depends(get_current_device),
    db: AsyncSession = Depends(get_db)
):
    return await process_batch_ingestion(payload, device, db)


@app.get("/api/v1/devices/me", tags=["Devices"])
async def get_my_device(device: Device = Depends(get_current_device)):
    return {
        "device_id": device.id,
        "active": device.active,
        "created_at": device.created_at,
        "last_seen_at": device.last_seen_at
    }


@app.get("/api/v1/devices/{device_id}/latest", response_model=LatestMetricResponse, tags=["Dashboard"])
async def get_latest_metrics(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    _auth: bool = Depends(verify_dashboard_auth)
):
    return await fetch_latest_metric(device_id, db)


@app.get("/api/v1/devices/{device_id}/records", response_model=PaginatedRecordsResponse, tags=["Dashboard"])
async def get_records(
    device_id: str,
    since_ms: Optional[int] = Query(None, description="Start timestamp in UTC epoch ms"),
    until_ms: Optional[int] = Query(None, description="End timestamp in UTC epoch ms"),
    cursor: Optional[int] = Query(None, description="Sequence cursor for pagination"),
    limit: int = Query(100, ge=1, le=500, description="Max records to return"),
    db: AsyncSession = Depends(get_db),
    _auth: bool = Depends(verify_dashboard_auth)
):
    return await fetch_historical_records(device_id, since_ms, until_ms, cursor, limit, db)


@app.get("/api/v1/devices/{device_id}/flags", response_model=FlagsResponse, tags=["Dashboard"])
async def get_flags(
    device_id: str,
    limit: int = Query(100, ge=1, le=500),
    db: AsyncSession = Depends(get_db),
    _auth: bool = Depends(verify_dashboard_auth)
):
    return await evaluate_device_flags(device_id, limit, db)


@app.delete("/api/v1/devices/{device_id}/records", tags=["Dashboard"])
async def delete_records(
    device_id: str,
    db: AsyncSession = Depends(get_db),
    _auth: bool = Depends(verify_dashboard_auth)
):
    from sqlalchemy import delete
    from .database import MetricRecord
    stmt = delete(MetricRecord).where(MetricRecord.device_id == device_id)
    await db.execute(stmt)
    await db.commit()
    return {"status": "cleared", "device_id": device_id}


# Mount dashboard static assets at root (evaluated after API routes)
dashboard_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "..", "dashboard")
if os.path.exists(dashboard_dir):
    app.mount("/", StaticFiles(directory=dashboard_dir, html=True), name="dashboard")

import os
from datetime import datetime, timezone
from typing import AsyncGenerator
from sqlalchemy import (
    Column, Integer, BigInteger, Float, String, Boolean, ForeignKey,
    UniqueConstraint, Index, select, JSON
)
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession, async_sessionmaker
from sqlalchemy.orm import declarative_base, relationship

DATABASE_URL = os.getenv("DATABASE_URL", "sqlite+aiosqlite:///./edgeecg.db")

engine = create_async_engine(
    DATABASE_URL,
    echo=False,
    future=True
)

AsyncSessionLocal = async_sessionmaker(
    bind=engine,
    class_=AsyncSession,
    expire_on_commit=False,
    autoflush=False
)

Base = declarative_base()


class Device(Base):
    __tablename__ = "devices"

    id = Column(String(64), primary_key=True, index=True)
    key_hash = Column(String(128), nullable=False)
    created_at = Column(BigInteger, nullable=False)
    last_seen_at = Column(BigInteger, nullable=True)
    active = Column(Boolean, default=True, nullable=False)

    records = relationship("MetricRecord", back_populates="device", cascade="all, delete-orphan")


class MetricRecord(Base):
    __tablename__ = "metric_records"

    id = Column(Integer, primary_key=True, autoincrement=True)
    device_id = Column(String(64), ForeignKey("devices.id", ondelete="CASCADE"), nullable=False)
    sequence = Column(BigInteger, nullable=False)
    session_id = Column(String(64), nullable=False)
    timestamp_ms = Column(BigInteger, nullable=False)
    received_at = Column(BigInteger, nullable=False)
    heart_rate_bpm = Column(Float, nullable=True)
    rmssd_ms = Column(Float, nullable=True)
    rr_interval_ms = Column(Float, nullable=True)
    signal_quality = Column(String(16), nullable=False, default="valid")
    detected_peaks = Column(Integer, nullable=False, default=0)
    rejected_peaks = Column(Integer, nullable=False, default=0)
    schema_version = Column(Integer, nullable=False, default=1)
    raw_samples = Column(JSON, nullable=True)

    device = relationship("Device", back_populates="records")

    __table_args__ = (
        UniqueConstraint("device_id", "sequence", name="uq_device_sequence"),
        Index("ix_device_timestamp", "device_id", "timestamp_ms"),
        Index("ix_device_sequence", "device_id", "sequence"),
    )


async def init_db():
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)


async def get_db() -> AsyncGenerator[AsyncSession, None]:
    async with AsyncSessionLocal() as session:
        try:
            yield session
        finally:
            await session.close()


def current_time_ms() -> int:
    return int(datetime.now(timezone.utc).timestamp() * 1000)

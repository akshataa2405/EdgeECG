from typing import List
from fastapi import HTTPException
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .database import Device, MetricRecord, current_time_ms
from .schemas import IngestRequest, IngestResponse


async def process_batch_ingestion(
    request: IngestRequest,
    device: Device,
    db: AsyncSession
) -> IngestResponse:
    if request.device_id != device.id:
        raise HTTPException(
            status_code=403,
            detail=f"Device ID '{request.device_id}' in payload does not match authenticated device '{device.id}'"
        )

    accepted_sequences: List[int] = []
    duplicate_sequences: List[int] = []
    rejected_records: List[dict] = []

    now_ms = current_time_ms()

    # Query all sequences present in this batch for this device in one pass
    batch_seqs = [r.sequence for r in request.records]
    stmt = select(MetricRecord.sequence).where(
        MetricRecord.device_id == device.id,
        MetricRecord.sequence.in_(batch_seqs)
    )
    result = await db.execute(stmt)
    existing_seqs = set(result.scalars().all())

    # Single atomic transaction
    for record_in in request.records:
        if record_in.sequence in existing_seqs:
            duplicate_sequences.append(record_in.sequence)
            continue

        new_record = MetricRecord(
            device_id=device.id,
            sequence=record_in.sequence,
            session_id=record_in.session_id,
            timestamp_ms=record_in.timestamp_ms,
            received_at=now_ms,
            heart_rate_bpm=record_in.heart_rate_bpm,
            rmssd_ms=record_in.rmssd_ms,
            rr_interval_ms=record_in.rr_interval_ms,
            signal_quality=record_in.signal_quality,
            detected_peaks=record_in.detected_peaks,
            rejected_peaks=record_in.rejected_peaks,
            schema_version=request.schema_version,
            raw_samples=record_in.raw_samples,
        )
        db.add(new_record)
        accepted_sequences.append(record_in.sequence)
        existing_seqs.add(record_in.sequence)

    # Update device last seen timestamp
    device.last_seen_at = now_ms

    await db.commit()

    return IngestResponse(
        status="accepted",
        accepted_sequences=accepted_sequences,
        duplicate_sequences=duplicate_sequences,
        rejected_records=rejected_records
    )

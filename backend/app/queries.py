from typing import Optional, List
from fastapi import HTTPException
from sqlalchemy import select, and_
from sqlalchemy.ext.asyncio import AsyncSession

from .database import Device, MetricRecord, current_time_ms
from .schemas import (
    LatestMetricResponse,
    PaginatedRecordsResponse,
    MetricRecordOut,
    FlagsResponse,
    FlaggedRecordOut
)


async def fetch_latest_metric(device_id: str, db: AsyncSession) -> LatestMetricResponse:
    device = await db.scalar(select(Device).where(Device.id == device_id))
    if not device:
        raise HTTPException(status_code=404, detail=f"Device '{device_id}' not found")

    stmt = (
        select(MetricRecord)
        .where(MetricRecord.device_id == device_id)
        .order_by(MetricRecord.sequence.desc())
        .limit(1)
    )
    latest_rec = await db.scalar(stmt)

    now_ms = current_time_ms()
    is_online = False
    if device.last_seen_at:
        # Considered online if pinged within the last 30 seconds
        is_online = (now_ms - device.last_seen_at) <= 30000

    rec_out = MetricRecordOut.model_validate(latest_rec) if latest_rec else None

    return LatestMetricResponse(
        device_id=device_id,
        last_seen_at=device.last_seen_at,
        is_online=is_online,
        latest_record=rec_out
    )


async def fetch_historical_records(
    device_id: str,
    since_ms: Optional[int],
    until_ms: Optional[int],
    cursor: Optional[int],
    limit: int,
    db: AsyncSession
) -> PaginatedRecordsResponse:
    # Cap limit to 500
    effective_limit = min(max(1, limit), 500)

    filters = [MetricRecord.device_id == device_id]
    if since_ms is not None:
        filters.append(MetricRecord.timestamp_ms >= since_ms)
    if until_ms is not None:
        filters.append(MetricRecord.timestamp_ms <= until_ms)
    if cursor is not None:
        filters.append(MetricRecord.sequence > cursor)

    # Fetch 1 extra to easily determine if next page exists
    stmt = (
        select(MetricRecord)
        .where(and_(*filters))
        .order_by(MetricRecord.sequence.asc())
        .limit(effective_limit + 1)
    )
    result = await db.execute(stmt)
    records = result.scalars().all()

    next_cursor = None
    if len(records) > effective_limit:
        records_to_return = records[:effective_limit]
        next_cursor = records_to_return[-1].sequence
    else:
        records_to_return = records

    out_records = [MetricRecordOut.model_validate(r) for r in records_to_return]

    return PaginatedRecordsResponse(
        device_id=device_id,
        records=out_records,
        next_cursor=next_cursor,
        total_returned=len(out_records)
    )


async def evaluate_device_flags(
    device_id: str,
    limit: int,
    db: AsyncSession
) -> FlagsResponse:
    stmt = (
        select(MetricRecord)
        .where(MetricRecord.device_id == device_id)
        .order_by(MetricRecord.sequence.desc())
        .limit(min(limit, 500))
    )
    result = await db.execute(stmt)
    records = list(reversed(result.scalars().all()))

    flagged: List[FlaggedRecordOut] = []
    prev_rr: Optional[float] = None

    for r in records:
        # Check rule 1: Bradycardia range (< 50 BPM)
        if r.heart_rate_bpm is not None and r.heart_rate_bpm < 50.0:
            flagged.append(FlaggedRecordOut(
                sequence=r.sequence,
                timestamp_ms=r.timestamp_ms,
                heart_rate_bpm=r.heart_rate_bpm,
                rr_interval_ms=r.rr_interval_ms,
                flag_type="bradycardia_range",
                description=f"Heart rate {r.heart_rate_bpm:.1f} BPM below 50 BPM threshold (rule-based flag)"
            ))

        # Check rule 2: Tachycardia range (> 120 BPM resting)
        if r.heart_rate_bpm is not None and r.heart_rate_bpm > 120.0:
            flagged.append(FlaggedRecordOut(
                sequence=r.sequence,
                timestamp_ms=r.timestamp_ms,
                heart_rate_bpm=r.heart_rate_bpm,
                rr_interval_ms=r.rr_interval_ms,
                flag_type="tachycardia_range",
                description=f"Heart rate {r.heart_rate_bpm:.1f} BPM exceeds 120 BPM threshold (rule-based flag)"
            ))

        # Check rule 3: Sudden RR Interval jump (> 20% jump between successive beats)
        if r.rr_interval_ms is not None and prev_rr is not None and prev_rr > 0:
            diff_pct = abs(r.rr_interval_ms - prev_rr) / prev_rr
            if diff_pct > 0.20:
                flagged.append(FlaggedRecordOut(
                    sequence=r.sequence,
                    timestamp_ms=r.timestamp_ms,
                    heart_rate_bpm=r.heart_rate_bpm,
                    rr_interval_ms=r.rr_interval_ms,
                    flag_type="rr_jump",
                    description=f"Sudden {diff_pct*100:.1f}% shift in successive RR interval ({prev_rr:.1f}ms -> {r.rr_interval_ms:.1f}ms)"
                ))

        if r.rr_interval_ms is not None:
            prev_rr = r.rr_interval_ms

    return FlagsResponse(
        device_id=device_id,
        total_flags=len(flagged),
        flags=flagged
    )

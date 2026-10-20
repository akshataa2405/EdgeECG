from typing import Literal, Optional, List
from pydantic import BaseModel, Field, ConfigDict


class MetricRecordIn(BaseModel):
    sequence: int = Field(..., ge=0, description="Monotonic sequence number assigned by firmware")
    session_id: str = Field(..., min_length=1, max_length=64, description="Firmware boot session identifier")
    timestamp_ms: int = Field(..., ge=0, description="UTC epoch milliseconds")
    heart_rate_bpm: Optional[float] = Field(None, ge=20.0, le=300.0, description="Instantaneous heart rate")
    rmssd_ms: Optional[float] = Field(None, ge=0.0, le=1000.0, description="Rolling RMSSD HRV metric")
    rr_interval_ms: Optional[float] = Field(None, ge=100.0, le=3000.0, description="RR interval")
    signal_quality: Literal["valid", "degraded", "invalid"] = Field(..., description="Signal quality state")
    detected_peaks: int = Field(0, ge=0)
    rejected_peaks: int = Field(0, ge=0)


class IngestRequest(BaseModel):
    schema_version: int = Field(1, ge=1, le=1, description="Schema version must be 1")
    device_id: str = Field(..., min_length=1, max_length=64)
    records: List[MetricRecordIn] = Field(..., min_length=1, max_length=100)


class IngestResponse(BaseModel):
    status: str = "accepted"
    accepted_sequences: List[int]
    duplicate_sequences: List[int]
    rejected_records: List[dict] = Field(default_factory=list)


class MetricRecordOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    sequence: int
    session_id: str
    timestamp_ms: int
    received_at: int
    heart_rate_bpm: Optional[float]
    rmssd_ms: Optional[float]
    rr_interval_ms: Optional[float]
    signal_quality: str
    detected_peaks: int
    rejected_peaks: int


class LatestMetricResponse(BaseModel):
    device_id: str
    last_seen_at: Optional[int]
    is_online: bool
    latest_record: Optional[MetricRecordOut]


class PaginatedRecordsResponse(BaseModel):
    device_id: str
    records: List[MetricRecordOut]
    next_cursor: Optional[int]
    total_returned: int


class FlaggedRecordOut(BaseModel):
    sequence: int
    timestamp_ms: int
    heart_rate_bpm: Optional[float]
    rr_interval_ms: Optional[float]
    flag_type: str
    description: str


class FlagsResponse(BaseModel):
    device_id: str
    total_flags: int
    flags: List[FlaggedRecordOut]


class HealthResponse(BaseModel):
    status: str
    timestamp_ms: int
    database: str

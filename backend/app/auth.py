import os
from typing import Optional
from dotenv import load_dotenv

load_dotenv()

from fastapi import Header, HTTPException, Depends, Security
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .database import Device, get_db, current_time_ms

import hashlib
import hmac

DASHBOARD_API_KEY = os.getenv("DASHBOARD_API_KEY", "")
security_bearer = HTTPBearer(auto_error=False)


def hash_key(plain_key: str) -> str:
    """Computes SHA-256 digest of plain credential."""
    return hashlib.sha256(plain_key.encode("utf-8")).hexdigest()


def verify_key(plain_key: str, hashed_key: str) -> bool:
    """Verifies plain key against stored hash in constant time."""
    candidate = hash_key(plain_key)
    return hmac.compare_digest(candidate, hashed_key)


async def get_current_device(
    x_device_key: Optional[str] = Header(None, alias="X-Device-Key"),
    db: AsyncSession = Depends(get_db)
) -> Device:
    if not x_device_key:
        raise HTTPException(status_code=401, detail="Missing X-Device-Key header")

    # Fetch all devices to verify hash
    result = await db.execute(select(Device))
    devices = result.scalars().all()

    matched_device: Optional[Device] = None
    for dev in devices:
        if verify_key(x_device_key, dev.key_hash):
            matched_device = dev
            break

    if not matched_device:
        raise HTTPException(status_code=401, detail="Invalid device credentials")

    if not matched_device.active:
        raise HTTPException(status_code=403, detail="Device is disabled")

    return matched_device


async def verify_dashboard_auth(
    credentials: Optional[HTTPAuthorizationCredentials] = Security(security_bearer)
):
    # Optional dashboard bearer token verification (allows local dev if not configured)
    if not DASHBOARD_API_KEY:
        return True

    if not credentials or credentials.credentials != DASHBOARD_API_KEY:
        # For ease of testing, also accept a query param or default if in local development
        if os.getenv("ENVIRONMENT") == "development":
            return True
        raise HTTPException(status_code=401, detail="Unauthorized dashboard access")
    return True


async def ensure_default_device(db: AsyncSession, device_id: str = "edgeecg-001", default_key: str = "edgeecg_device_secret_sample_key"):
    result = await db.execute(select(Device).where(Device.id == device_id))
    dev = result.scalar_one_or_none()
    if not dev:
        dev = Device(
            id=device_id,
            key_hash=hash_key(default_key),
            created_at=current_time_ms(),
            last_seen_at=None,
            active=True
        )
        db.add(dev)
        await db.commit()

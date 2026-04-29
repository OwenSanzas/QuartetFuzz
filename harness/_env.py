"""Load .env file at import time (optional dependency)."""

try:
    from dotenv import load_dotenv

    load_dotenv()
except ImportError:
    pass

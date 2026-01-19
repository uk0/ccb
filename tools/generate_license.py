#!/usr/bin/env python3
"""
CCB License Generator

This script generates license keys for Claude Code Proxy (CCB).

Usage:
    python generate_license.py <machine_id> <expiration_date>

Arguments:
    machine_id: The machine ID from the CCB application (format: XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX)
    expiration_date: Expiration date in YYYY-MM-DD format

Example:
    python generate_license.py "A1B2C3D4-E5F6G7H8-I9J0K1L2-M3N4O5P6" "2025-12-31"

Output:
    A license key that can be entered in the CCB application.
"""

import hashlib
import sys
from datetime import datetime
from typing import Tuple

# Secret key - must match the one in C++ code
SECRET_KEY = b"CCB_LICENSE_KEY_2024_FIRSH_ME"

# Custom Base32 alphabet (32 unique characters, avoiding I, L, O)
BASE32_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ234567890"


def base32_encode(data: bytes) -> str:
    """Custom Base32 encoding."""
    result = []
    buffer = 0
    bits_left = 0

    for byte in data:
        buffer = (buffer << 8) | byte
        bits_left += 8
        while bits_left >= 5:
            bits_left -= 5
            index = (buffer >> bits_left) & 0x1F
            result.append(BASE32_ALPHABET[index])
            # Clear the bits we've used
            buffer &= (1 << bits_left) - 1

    if bits_left > 0:
        index = (buffer << (5 - bits_left)) & 0x1F
        result.append(BASE32_ALPHABET[index])

    return ''.join(result)


def format_license_key(key: str) -> str:
    """Format license key with dashes for readability."""
    # Remove any existing formatting
    key = key.replace('-', '').replace(' ', '')
    # Add dashes every 5 characters
    parts = [key[i:i+5] for i in range(0, len(key), 5)]
    return '-'.join(parts)


def generate_license(machine_id: str, expiration_date: str) -> Tuple[str, dict]:
    """
    Generate a license key for the given machine ID and expiration date.

    Args:
        machine_id: Machine ID from the CCB application
        expiration_date: Expiration date in YYYY-MM-DD format

    Returns:
        Tuple of (license_key, info_dict)
    """
    # Parse and validate expiration date
    try:
        exp_date = datetime.strptime(expiration_date, "%Y-%m-%d")
    except ValueError:
        raise ValueError(f"Invalid date format: {expiration_date}. Use YYYY-MM-DD.")

    # Calculate days since epoch
    epoch = datetime(1970, 1, 1)
    expire_days = (exp_date - epoch).days

    if expire_days < 0:
        raise ValueError("Expiration date cannot be before 1970-01-01")

    # Clean machine ID (remove dashes)
    clean_machine_id = machine_id.replace('-', '').replace(' ', '').upper()

    # Validate machine ID (should be 32 hex characters)
    if len(clean_machine_id) != 32:
        raise ValueError(f"Invalid machine ID length: {len(clean_machine_id)} (expected 32 characters)")

    # Validate hex characters
    try:
        int(clean_machine_id, 16)
    except ValueError:
        raise ValueError(f"Machine ID must be hexadecimal characters (0-9, A-F)")

    # Generate machine ID hash (first 8 bytes of SHA256)
    machine_hash = hashlib.sha256(clean_machine_id.encode('utf-8') + SECRET_KEY).digest()[:8]

    # Convert expiration days to 4 bytes (big-endian)
    expire_bytes = expire_days.to_bytes(4, byteorder='big')

    # Generate signature (first 4 bytes of SHA256)
    sign_data = machine_hash + expire_bytes
    signature = hashlib.sha256(sign_data + SECRET_KEY).digest()[:4]

    # Combine all parts: machineIdHash(8) + expireDate(4) + signature(4) = 16 bytes
    license_data = machine_hash + expire_bytes + signature

    # Encode to Base32
    license_key = base32_encode(license_data)

    # Format with dashes
    formatted_key = format_license_key(license_key)

    info = {
        'machine_id': machine_id,
        'expiration_date': expiration_date,
        'expire_days': expire_days,
        'license_key': formatted_key,
        'raw_key': license_key
    }

    return formatted_key, info


def main():
    """Main entry point."""
    print("=" * 60)
    print("CCB License Generator")
    print("=" * 60)

    if len(sys.argv) == 3:
        # Command line arguments provided
        machine_id = sys.argv[1]
        expiration_date = sys.argv[2]
    else:
        # Interactive mode
        print("\nEnter the following information:\n")

        machine_id = input("Machine ID: ").strip()
        if not machine_id:
            print("Error: Machine ID is required.")
            sys.exit(1)

        expiration_date = input("Expiration Date (YYYY-MM-DD): ").strip()
        if not expiration_date:
            print("Error: Expiration date is required.")
            sys.exit(1)

    try:
        license_key, info = generate_license(machine_id, expiration_date)

        print("\n" + "=" * 60)
        print("LICENSE GENERATED SUCCESSFULLY")
        print("=" * 60)
        print(f"\nMachine ID:       {info['machine_id']}")
        print(f"Expiration Date:  {info['expiration_date']}")
        print(f"\nLicense Key:")
        print("-" * 40)
        print(f"\n  {license_key}\n")
        print("-" * 40)
        print("\nCopy the license key above and enter it in the CCB application.")
        print("=" * 60)

    except ValueError as e:
        print(f"\nError: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\nUnexpected error: {e}")
        sys.exit(1)


def batch_generate(licenses: list) -> list:
    """
    Generate multiple licenses.

    Args:
        licenses: List of tuples (machine_id, expiration_date)

    Returns:
        List of (machine_id, expiration_date, license_key) tuples
    """
    results = []
    for machine_id, expiration_date in licenses:
        try:
            license_key, _ = generate_license(machine_id, expiration_date)
            results.append((machine_id, expiration_date, license_key))
        except Exception as e:
            results.append((machine_id, expiration_date, f"ERROR: {e}"))
    return results


if __name__ == "__main__":
    main()

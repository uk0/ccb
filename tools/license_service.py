#!/usr/bin/env python3
"""
CCB License Service

A simple web service for generating CCB license keys.

Usage:
    python license_service.py [port]

Default port: 5000
"""

import hashlib
import sys
from datetime import datetime
from flask import Flask, request

app = Flask(__name__)

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
            buffer &= (1 << bits_left) - 1

    if bits_left > 0:
        index = (buffer << (5 - bits_left)) & 0x1F
        result.append(BASE32_ALPHABET[index])

    return ''.join(result)


def format_license_key(key: str) -> str:
    """Format license key with dashes for readability."""
    key = key.replace('-', '').replace(' ', '')
    parts = [key[i:i+5] for i in range(0, len(key), 5)]
    return '-'.join(parts)


def generate_license(machine_id: str, expiration_date: str) -> tuple:
    """Generate a license key for the given machine ID and expiration date."""
    # Parse expiration date
    exp_date = datetime.strptime(expiration_date, "%Y-%m-%d")

    # Calculate days since epoch
    epoch = datetime(1970, 1, 1)
    expire_days = (exp_date - epoch).days

    # Clean machine ID
    clean_machine_id = machine_id.replace('-', '').replace(' ', '').upper()

    # Generate machine ID hash (first 8 bytes of SHA256)
    machine_hash = hashlib.sha256(clean_machine_id.encode('utf-8') + SECRET_KEY).digest()[:8]

    # Convert expiration days to 4 bytes (big-endian)
    expire_bytes = expire_days.to_bytes(4, byteorder='big')

    # Generate signature (first 4 bytes of SHA256)
    sign_data = machine_hash + expire_bytes
    signature = hashlib.sha256(sign_data + SECRET_KEY).digest()[:4]

    # Combine all parts
    license_data = machine_hash + expire_bytes + signature

    # Encode to Base32
    license_key = base32_encode(license_data)

    return format_license_key(license_key), expire_days


def validate_machine_id(machine_id: str) -> tuple:
    """Validate machine ID format. Returns (is_valid, error_message)."""
    clean_id = machine_id.replace('-', '').replace(' ', '').upper()

    if len(clean_id) != 32:
        return False, f"Machine ID must be 32 hex characters (got {len(clean_id)})"

    try:
        int(clean_id, 16)
    except ValueError:
        return False, "Machine ID must contain only hexadecimal characters (0-9, A-F)"

    return True, ""


@app.route('/', methods=['GET', 'POST'])
def index():
    result_html = ""
    machine_id_value = ""
    expiration_value = ""

    if request.method == 'POST':
        machine_id = request.form.get('machine_id', '').strip()
        expiration_date = request.form.get('expiration_date', '').strip()

        machine_id_value = machine_id
        expiration_value = expiration_date

        # Validate inputs
        error = None

        if not machine_id:
            error = "Machine ID is required"
        elif not expiration_date:
            error = "Expiration date is required"
        else:
            is_valid, err_msg = validate_machine_id(machine_id)
            if not is_valid:
                error = err_msg
            else:
                try:
                    datetime.strptime(expiration_date, "%Y-%m-%d")
                except ValueError:
                    error = "Invalid date format. Use YYYY-MM-DD"

        if error:
            result_html = f'''
            <table border="1" cellpadding="10" bgcolor="#ffcccc">
                <tr>
                    <td><b>Error:</b> {error}</td>
                </tr>
            </table>
            '''
        else:
            try:
                license_key, expire_days = generate_license(machine_id, expiration_date)
                result_html = f'''
                <table border="1" cellpadding="10" bgcolor="#ccffcc">
                    <tr>
                        <td colspan="2" align="center"><b>License Generated Successfully</b></td>
                    </tr>
                    <tr>
                        <td><b>Machine ID:</b></td>
                        <td><code>{machine_id}</code></td>
                    </tr>
                    <tr>
                        <td><b>Expiration:</b></td>
                        <td>{expiration_date} (day {expire_days})</td>
                    </tr>
                    <tr>
                        <td><b>License Key:</b></td>
                        <td>
                            <code id="license-key" style="font-size:16px;font-weight:bold">{license_key}</code>
                            <br><br>
                            <button onclick="copyLicense()">Copy to Clipboard</button>
                        </td>
                    </tr>
                </table>
                <script>
                function copyLicense() {{
                    var key = document.getElementById('license-key').innerText;
                    navigator.clipboard.writeText(key);
                    alert('License key copied!');
                }}
                </script>
                '''
            except Exception as e:
                result_html = f'''
                <table border="1" cellpadding="10" bgcolor="#ffcccc">
                    <tr>
                        <td><b>Error:</b> {str(e)}</td>
                    </tr>
                </table>
                '''

    html = f'''<!DOCTYPE html>
<html>
<head>
    <title>CCB License Generator</title>
    <meta charset="UTF-8">
</head>
<body>
    <table border="0" cellpadding="20" align="center">
        <tr>
            <td>
                <h1>CCB License Generator</h1>
                <hr>

                <form method="POST">
                    <table border="0" cellpadding="5">
                        <tr>
                            <td><b>Machine ID:</b></td>
                            <td>
                                <input type="text" name="machine_id" size="50"
                                       placeholder="XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX"
                                       value="{machine_id_value}">
                            </td>
                        </tr>
                        <tr>
                            <td><b>Expiration Date:</b></td>
                            <td>
                                <input type="date" name="expiration_date" value="{expiration_value}">
                            </td>
                        </tr>
                        <tr>
                            <td></td>
                            <td>
                                <br>
                                <input type="submit" value="Generate License">
                                <input type="reset" value="Clear">
                            </td>
                        </tr>
                    </table>
                </form>

                <br>
                {result_html}

                <hr>
                <table border="0" cellpadding="5">
                    <tr>
                        <td><small>CCB License Service v1.0 | Claude Code Proxy</small></td>
                    </tr>
                </table>
            </td>
        </tr>
    </table>
</body>
</html>'''

    return html


@app.route('/api/generate', methods=['POST'])
def api_generate():
    """API endpoint for license generation."""
    import json

    data = request.get_json() or {}
    machine_id = data.get('machine_id', '').strip()
    expiration_date = data.get('expiration_date', '').strip()

    if not machine_id or not expiration_date:
        return json.dumps({
            'success': False,
            'error': 'machine_id and expiration_date are required'
        }), 400, {'Content-Type': 'application/json'}

    is_valid, err_msg = validate_machine_id(machine_id)
    if not is_valid:
        return json.dumps({
            'success': False,
            'error': err_msg
        }), 400, {'Content-Type': 'application/json'}

    try:
        datetime.strptime(expiration_date, "%Y-%m-%d")
    except ValueError:
        return json.dumps({
            'success': False,
            'error': 'Invalid date format. Use YYYY-MM-DD'
        }), 400, {'Content-Type': 'application/json'}

    try:
        license_key, expire_days = generate_license(machine_id, expiration_date)
        return json.dumps({
            'success': True,
            'machine_id': machine_id,
            'expiration_date': expiration_date,
            'expire_days': expire_days,
            'license_key': license_key
        }), 200, {'Content-Type': 'application/json'}
    except Exception as e:
        return json.dumps({
            'success': False,
            'error': str(e)
        }), 500, {'Content-Type': 'application/json'}


@app.route('/api/batch', methods=['POST'])
def api_batch():
    """Batch license generation API."""
    import json

    data = request.get_json() or {}
    licenses = data.get('licenses', [])

    if not licenses:
        return json.dumps({
            'success': False,
            'error': 'licenses array is required'
        }), 400, {'Content-Type': 'application/json'}

    results = []
    for item in licenses:
        machine_id = item.get('machine_id', '').strip()
        expiration_date = item.get('expiration_date', '').strip()

        try:
            license_key, expire_days = generate_license(machine_id, expiration_date)
            results.append({
                'machine_id': machine_id,
                'expiration_date': expiration_date,
                'license_key': license_key,
                'success': True
            })
        except Exception as e:
            results.append({
                'machine_id': machine_id,
                'expiration_date': expiration_date,
                'error': str(e),
                'success': False
            })

    return json.dumps({
        'success': True,
        'results': results
    }), 200, {'Content-Type': 'application/json'}


def main():
    port = 5000
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print(f"Invalid port: {sys.argv[1]}")
            sys.exit(1)

    print("=" * 50)
    print("CCB License Service")
    print("=" * 50)
    print(f"\nStarting server on http://0.0.0.0:{port}")
    print("\nEndpoints:")
    print(f"  Web UI:     http://0.0.0.0:{port}/")
    print(f"  API:        POST http://0.0.0.0:{port}/api/generate")
    print(f"  Batch API:  POST http://0.0.0.0:{port}/api/batch")
    print("\nPress Ctrl+C to stop\n")

    app.run(host='0.0.0.0', port=port, debug=False)


if __name__ == "__main__":
    main()

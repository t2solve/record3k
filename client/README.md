# Live Bug Tracker 3000 - Client Setup

A minimal web client for the Testrecord3k API that displays and manages cameras.

## Quick Start

### 1. Start the API Server

First, make sure the API server is running:

```bash
cd build/bin
./apiserver
```

You should see:
```
[api] Loaded drogon.config.json
[api] Starting Drogon server; swagger (if enabled) at /swagger and spec at /openapi.json
[api] CORS enabled for browser access
```

The API will be available at: `http://127.0.0.1:8080`

### 2. Rebuild API Server (if CORS changes were made)

If you just added CORS support, rebuild the API server:

```bash
cd build/src/api
make -j8 apiserver
```

Then restart the server from `build/bin/`.

### 3. Start the Client Development Server

From the `client/` directory:

```bash
cd client
python3 serve.py
```

Or directly:

```bash
python3 client/serve.py
```

The client will be available at: `http://localhost:3000`

## Testing

### Test API Endpoint Directly

```bash
# List cameras
curl http://127.0.0.1:8080/info/cameras/list

# Get OpenAPI spec
curl http://127.0.0.1:8080/openapi.json

# View Swagger UI
open http://127.0.0.1:8080/swagger
```

### Test CORS

```bash
curl -H "Origin: http://localhost:3000" \
     -H "Access-Control-Request-Method: GET" \
     -X OPTIONS \
     http://127.0.0.1:8080/info/cameras/list -v
```

You should see `Access-Control-Allow-Origin: *` in the response headers.

## Features

- **Camera List**: Displays all available cameras with status indicators
- **Real-time Status**: Shows connection status to the API
- **Calibration**: Start calibration directly from the UI
- **Responsive Design**: Works on desktop and mobile

## Troubleshooting

### "Failed to fetch" Error

1. **Check API server is running**: Visit `http://127.0.0.1:8080/info/cameras/list` in your browser
2. **CORS issues**: Make sure you rebuilt the API server after adding CORS headers
3. **Port conflict**: Ensure port 8080 is not blocked by firewall

### API Returns 404

1. **Check route paths**: Routes should use plural form (e.g., `/info/cameras/list`, not `/info/camera/list`)
2. **Rebuild**: After changing controller routes, rebuild with `make apiserver`

### Styling Issues

1. Ensure `style.css` is in the same directory as `index.html`
2. Clear browser cache (Ctrl+Shift+R / Cmd+Shift+R)

## File Structure

```
client/
├── index.html    # Main HTML structure
├── app.js        # JavaScript application logic
├── style.css     # Styling and responsive design
├── serve.py      # Python development server
└── README.md     # This file
```

## API Endpoints Used

- `GET /info/cameras/list` - List all cameras
- `POST /do/camera/{camUID}/calibrate` - Start calibration
- `GET /openapi.json` - OpenAPI specification
- `GET /swagger` - Swagger UI

## Next Steps

- Add more views (pipelines, records, calibrations)
- Implement real-time updates with WebSocket
- Add filtering and search functionality
- Persist API base URL in localStorage

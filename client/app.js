// Live Bug Tracker 3000 - Main Application Logic
const API_BASE_URL = 'http://127.0.0.1:8080';

class BugTrackerApp {
    constructor() {
        this.cameras = [];
        this.records = [];
        this.init();
    }

    init() {
        // Event listeners
        document.getElementById('refresh-btn').addEventListener('click', () => this.loadCameras());
        document.getElementById('refresh-records-btn').addEventListener('click', () => this.loadRecords());
        
        // Initial load
        this.checkApiStatus();
        this.loadCameras();
        this.loadRecords();
    }

    async checkApiStatus() {
        const statusEl = document.getElementById('api-status');
        try {
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`);
            if (response.ok) {
                statusEl.textContent = 'Connected ✓';
                statusEl.className = 'status-connected';
            } else {
                statusEl.textContent = 'API Error';
                statusEl.className = 'status-error';
            }
        } catch (error) {
            statusEl.textContent = 'Disconnected ✗';
            statusEl.className = 'status-disconnected';
        }
    }

    async loadCameras() {
        const loadingEl = document.getElementById('loading');
        const errorEl = document.getElementById('error');
        const cameraListEl = document.getElementById('camera-list');

        // Show loading state
        loadingEl.style.display = 'block';
        errorEl.style.display = 'none';
        cameraListEl.innerHTML = '';

        try {
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`);
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            this.cameras = await response.json();
            this.renderCameras();
            
        } catch (error) {
            console.error('Failed to load cameras:', error);
            errorEl.textContent = `Failed to load cameras: ${error.message}`;
            errorEl.style.display = 'block';
        } finally {
            loadingEl.style.display = 'none';
        }
    }

    renderCameras() {
        const cameraListEl = document.getElementById('camera-list');
        
        if (this.cameras.length === 0) {
            cameraListEl.innerHTML = '<p class="no-data">No cameras available</p>';
            return;
        }

        cameraListEl.innerHTML = this.cameras.map(camera => `
            <div class="camera-card ${camera.status === 'online' ? 'status-online' : 'status-offline'}">
                <div class="camera-header">
                    <h3>${this.escapeHtml(camera.description || 'Unknown Camera')}</h3>
                    <span class="status-badge ${camera.status}">${camera.status}</span>
                </div>
                <div class="camera-details">
                    <div class="detail-row">
                        <span class="label">Camera UID:</span>
                        <span class="value"><code>${this.escapeHtml(camera.camUID)}</code></span>
                    </div>
                    <div class="detail-row">
                        <span class="label">MAC Address:</span>
                        <span class="value"><code>${this.escapeHtml(camera.macAddress)}</code></span>
                    </div>
                </div>
                <div class="camera-actions">
                    <button class="btn-action" onclick="app.viewCamera('${this.escapeHtml(camera.camUID)}')">
                        📷 View Details
                    </button>
                    <button class="btn-action" onclick="app.calibrateCamera('${this.escapeHtml(camera.camUID)}')">
                        🎯 Calibrate
                    </button>
                </div>
            </div>
        `).join('');
    }

    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    async viewCamera(camUID) {
        alert(`View details for camera: ${camUID}\n(Feature coming soon)`);
    }

    async calibrateCamera(camUID) {
        if (!confirm(`Start calibration for camera ${camUID}?`)) return;
        
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/${camUID}/calibrate`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ description: 'Web UI calibration' })
            });
            
            if (response.ok) {
                const result = await response.json();
                alert(`Calibration started!\nCalibration UID: ${result.calibrationUID}`);
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            alert(`Failed to start calibration: ${error.message}`);
        }
    }
}

// Initialize app when DOM is ready
let app;
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        app = new BugTrackerApp();
    });
} else {
    app = new BugTrackerApp();
}

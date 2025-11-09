// Live Bug Tracker 3000 - Main Application Logic
const API_BASE_URL = 'http://127.0.0.1:8080';

class BugTrackerApp {
    constructor() {
        this.cameras = [];
        this.records = [];
        this.currentPage = this.detectPage();
        this.init();
    }

    detectPage() {
        const path = window.location.pathname;
        if (path.includes('records.html')) return 'records';
        if (path.includes('start.html')) return 'start';
        return 'cameras';
    }

    init() {
        // Check API status (on all pages)
        this.checkApiStatus();

        // Page-specific initialization
        if (this.currentPage === 'cameras') {
            document.getElementById('refresh-btn')?.addEventListener('click', () => this.loadCameras());
            document.getElementById('update-btn')?.addEventListener('click', () => this.updateCameraList());
            this.loadCameras();
        } else if (this.currentPage === 'records') {
            document.getElementById('refresh-records-btn')?.addEventListener('click', () => this.loadRecords());
            this.loadRecords();
        } else if (this.currentPage === 'start') {
            this.initStartRecordingPage();
        }
    }

    // Start Recording Page - Initialization
    async initStartRecordingPage() {
        // Load cameras into dropdown
        await this.loadCamerasForStart();
        
        // Setup event listeners
        const cameraSelect = document.getElementById('camera-select');
        const calibrationSelect = document.getElementById('calibration-select');
        const pipelineSelect = document.getElementById('pipeline-select');
        const form = document.getElementById('recording-form');

        cameraSelect.addEventListener('change', () => this.onCameraSelected());
        calibrationSelect.addEventListener('change', () => this.onCalibrationSelected());
        pipelineSelect.addEventListener('change', () => this.onPipelineSelected());
        form.addEventListener('submit', (e) => this.onStartRecording(e));
    }

    async loadCamerasForStart() {
        const cameraSelect = document.getElementById('camera-select');
        try {
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const cameras = await response.json();
            cameraSelect.innerHTML = '<option value="">-- Select a camera --</option>' +
                cameras.map(cam => `<option value="${this.escapeHtml(cam.camUID)}">${this.escapeHtml(cam.description || cam.camUID)}</option>`).join('');
        } catch (error) {
            cameraSelect.innerHTML = `<option value="">Error loading cameras</option>`;
            console.error('Failed to load cameras:', error);
        }
    }

    async onCameraSelected() {
        const cameraSelect = document.getElementById('camera-select');
        const camUID = cameraSelect.value;
        
        if (!camUID) {
            document.getElementById('calibration-select').disabled = true;
            document.getElementById('pipeline-select').disabled = true;
            return;
        }

        // Load calibrations for selected camera
        await this.loadCalibrationsForCamera(camUID);
        
        // Load pipelines for selected camera
        await this.loadPipelinesForCamera(camUID);
    }

    async loadCalibrationsForCamera(camUID) {
        const calibrationSelect = document.getElementById('calibration-select');
        calibrationSelect.disabled = false;
        calibrationSelect.innerHTML = '<option value="">Loading...</option>';
        
        try {
            const response = await fetch(`${API_BASE_URL}/info/calibrations/list`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const allCalibrations = await response.json();
            // Filter calibrations for this camera
            const calibrations = allCalibrations.filter(cal => cal.cameraUID === camUID);
            
            if (calibrations.length === 0) {
                calibrationSelect.innerHTML = '<option value="">No calibrations available</option>';
                return;
            }

            // Sort by timestamp descending (latest first)
            calibrations.sort((a, b) => new Date(b.datetimeCreated) - new Date(a.datetimeCreated));
            
            // Populate dropdown
            calibrationSelect.innerHTML = calibrations.map((cal, idx) => 
                `<option value="${this.escapeHtml(cal.calibrationUID)}" ${idx === 0 ? 'selected' : ''}>
                    ${this.escapeHtml(cal.calibrationUID)} - ${new Date(cal.datetimeCreated).toLocaleString()}
                </option>`
            ).join('');
            
            // Auto-select first (latest) calibration
            if (calibrations.length > 0) {
                calibrationSelect.value = calibrations[0].calibrationUID;
                this.onCalibrationSelected();
            }
        } catch (error) {
            calibrationSelect.innerHTML = '<option value="">Error loading calibrations</option>';
            console.error('Failed to load calibrations:', error);
        }
    }

    async onCalibrationSelected() {
        const calibrationSelect = document.getElementById('calibration-select');
        const calibrationUID = calibrationSelect.value;
        
        if (!calibrationUID) {
            document.getElementById('calibration-info').style.display = 'none';
            return;
        }

        // Fetch calibration details
        try {
            const response = await fetch(`${API_BASE_URL}/info/calibrations/get/${calibrationUID}`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const calibration = await response.json();
            
            // Display calibration info
            document.getElementById('cal-date').textContent = new Date(calibration.datetimeCreated).toLocaleString();
            document.getElementById('cal-desc').textContent = calibration.description || 'N/A';
            document.getElementById('cal-status').textContent = calibration.status;
            document.getElementById('calibration-info').style.display = 'block';
        } catch (error) {
            console.error('Failed to load calibration details:', error);
            document.getElementById('calibration-info').style.display = 'none';
        }
    }

    async loadPipelinesForCamera(camUID) {
        const pipelineSelect = document.getElementById('pipeline-select');
        pipelineSelect.disabled = false;
        pipelineSelect.innerHTML = '<option value="">Loading...</option>';
        
        try {
            const response = await fetch(`${API_BASE_URL}/info/pipelines/list`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const pipelines = await response.json();
            
            if (pipelines.length === 0) {
                pipelineSelect.innerHTML = '<option value="">No pipelines available</option>';
                return;
            }

            // Populate dropdown (latest first if they have timestamps)
            pipelineSelect.innerHTML = pipelines.map((pipe, idx) => 
                `<option value="${this.escapeHtml(pipe.pipelineUID)}" ${idx === 0 ? 'selected' : ''}>
                    ${this.escapeHtml(pipe.pipelineUID)}
                </option>`
            ).join('');
            
            // Auto-select first pipeline
            if (pipelines.length > 0) {
                pipelineSelect.value = pipelines[0].pipelineUID;
                this.onPipelineSelected();
            }
        } catch (error) {
            pipelineSelect.innerHTML = '<option value="">Error loading pipelines</option>';
            console.error('Failed to load pipelines:', error);
        }
    }

    async onPipelineSelected() {
        const pipelineSelect = document.getElementById('pipeline-select');
        const pipelineUID = pipelineSelect.value;
        
        if (!pipelineUID) {
            document.getElementById('pipeline-info').style.display = 'none';
            return;
        }

        // Fetch pipeline details
        try {
            const response = await fetch(`${API_BASE_URL}/info/pipelines/get/${pipelineUID}`);
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const pipeline = await response.json();
            
            // Display pipeline info
            document.getElementById('pipe-name').textContent = pipeline.pipelineUID;
            document.getElementById('pipe-desc').textContent = pipeline.description || 'N/A';
            document.getElementById('pipe-status').textContent = `Status: ${pipeline.status}`;
            document.getElementById('pipeline-info').style.display = 'block';
        } catch (error) {
            console.error('Failed to load pipeline details:', error);
            document.getElementById('pipeline-info').style.display = 'none';
        }
    }

    async onStartRecording(event) {
        event.preventDefault();
        
        const statusEl = document.getElementById('form-status');
        const startBtn = document.getElementById('start-btn');
        
        // Disable button to prevent double-click
        startBtn.disabled = true;
        statusEl.style.display = 'block';
        statusEl.className = 'form-status status-info';
        statusEl.textContent = 'Starting recording...';

        try {
            // Collect form data
            const camUID = document.getElementById('camera-select').value;
            const calibrationUID = document.getElementById('calibration-select').value;
            const pipelineUID = document.getElementById('pipeline-select').value;
            const studyDesc = document.getElementById('study-desc').value.trim();
            const scientificName = document.getElementById('scientific-name').value.trim();
            const weightInMg = parseFloat(document.getElementById('weight').value);
            
            // Calculate total duration in seconds from hours, minutes, seconds
            const hours = parseInt(document.getElementById('duration-hours').value) || 0;
            const minutes = parseInt(document.getElementById('duration-minutes').value) || 0;
            const seconds = parseInt(document.getElementById('duration-seconds').value) || 0;
            const durationInSeconds = (hours * 3600) + (minutes * 60) + seconds;
            
            const recordDesc = document.getElementById('record-desc').value.trim();

            // Validation
            if (!camUID || !calibrationUID || !pipelineUID || !scientificName || !weightInMg || durationInSeconds < 1) {
                throw new Error('Please fill in all required fields and ensure duration is at least 1 second');
            }

            // Step 1: Create study meta info to get auto-generated studyInfoUID
            statusEl.textContent = 'Creating study meta info...';
            
            const studyPayload = {
                description: studyDesc,
                individualScientificName: scientificName,
                weightInMg: weightInMg
            };
            console.log('Sending to /add/studyMetaInfo:', studyPayload);
            
            const studyResponse = await fetch(`${API_BASE_URL}/add/studyMetaInfo`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(studyPayload)
            });

            if (!studyResponse.ok) {
                const errorText = await studyResponse.text();
                throw new Error(`Failed to create study meta info: HTTP ${studyResponse.status} - ${errorText}`);
            }

            const studyResult = await studyResponse.json();
            const studyInfoUID = studyResult.studyInfoUID;
            console.log('Created study UID:', studyInfoUID);

            // Step 2: Start recording
            statusEl.textContent = 'Starting recording session...';
            
            const recordPayload = {
                camUID: camUID,
                durationInMS: durationInSeconds * 1000,
                calibrationUID: calibrationUID,
                pipelineUID: pipelineUID,
                studyInfoUID: studyInfoUID,
                description: recordDesc
            };
            console.log('Sending to /do/record/start:', recordPayload);
            
            const recordResponse = await fetch(`${API_BASE_URL}/do/camera/record/start/${encodeURIComponent(camUID)}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(recordPayload)
            });

            if (!recordResponse.ok) {
                const errorText = await recordResponse.text();
                throw new Error(`Failed to start recording: HTTP ${recordResponse.status} - ${errorText}`);
            }

            const recordResult = await recordResponse.json();
            
            // Success!
            statusEl.className = 'form-status status-success';
            statusEl.textContent = `✓ Recording started successfully! Record UID: ${recordResult.recordUID}`;
            
            // Redirect to records page after 2 seconds
            setTimeout(() => {
                window.location.href = 'records.html';
            }, 2000);

        } catch (error) {
            statusEl.className = 'form-status status-error';
            statusEl.textContent = `✗ Error: ${error.message}`;
            startBtn.disabled = false;
            console.error('Failed to start recording:', error);
        }
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

    async updateCameraList() {
        const updateBtn = document.getElementById('update-btn');
        const originalText = updateBtn.textContent;
        updateBtn.disabled = true;
        updateBtn.textContent = '⏳ Updating...';
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/updatelist`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            // Optionally could read response body if it returns JSON summary
            await this.loadCameras();
        } catch (err) {
            console.error('Failed to update camera list:', err);
            alert(`Failed to update camera list: ${err.message}`);
        } finally {
            updateBtn.disabled = false;
            updateBtn.textContent = originalText;
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
                    <div class="detail-row">
                        <span class="label">Type:</span>
                        <span class="value"><code>${this.escapeHtml(camera.cameraType || 'unknown')}</code></span>
                    </div>
                    <div class="detail-row">
                        <span class="label">Last seen:</span>
                        <span class="value">${camera.datetimeLastSeen ? new Date(camera.datetimeLastSeen).toLocaleString() : 'N/A'}</span>
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
            const response = await fetch(`${API_BASE_URL}/do/camera/calibrate/${camUID}`, {
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

    async loadRecords() {
        const loadingEl = document.getElementById('records-loading');
        const errorEl = document.getElementById('records-error');
        const recordsListEl = document.getElementById('records-list');

        // Show loading state
        loadingEl.style.display = 'block';
        errorEl.style.display = 'none';
        recordsListEl.innerHTML = '';

        try {
            const response = await fetch(`${API_BASE_URL}/info/records/list`);
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            this.records = await response.json();
            this.renderRecords();
            
        } catch (error) {
            console.error('Failed to load records:', error);
            errorEl.textContent = `Failed to load records: ${error.message}`;
            errorEl.style.display = 'block';
        } finally {
            loadingEl.style.display = 'none';
        }
    }

    renderRecords() {
        const recordsListEl = document.getElementById('records-list');
        
        if (this.records.length === 0) {
            recordsListEl.innerHTML = '<p class="no-data">No recording sessions found</p>';
            return;
        }

        recordsListEl.innerHTML = this.records.map(record => {
            const duration = record.durationInSeconds ? `${record.durationInSeconds.toFixed(1)}s` : 'N/A';
            const startTime = record.startTime ? new Date(record.startTime).toLocaleString() : 'N/A';
            const endTime = record.endTime ? new Date(record.endTime).toLocaleString() : 'In Progress';
            const statusClass = record.status === 'finished' ? 'status-finished' : 
                               record.status === 'running' ? 'status-running' : 'status-other';
            
            return `
                <div class="record-card ${statusClass}">
                    <div class="record-header">
                        <div class="record-title">
                            <h3>${record.isTest ? '🧪 Test' : '📹 Recording'}: ${this.escapeHtml(record.recordUID)}</h3>
                            <span class="status-badge ${record.status}">${record.status}</span>
                        </div>
                        <div class="record-meta">
                            <span class="duration">${duration}</span>
                        </div>
                    </div>
                    <div class="record-details">
                        <div class="detail-row">
                            <span class="label">Pipeline:</span>
                            <span class="value"><code>${this.escapeHtml(record.pipelineUID)}</code></span>
                        </div>
                        <div class="detail-row">
                            <span class="label">Calibration:</span>
                            <span class="value"><code>${this.escapeHtml(record.calibrationUID)}</code></span>
                        </div>
                        <div class="detail-row">
                            <span class="label">Study:</span>
                            <span class="value"><code>${this.escapeHtml(record.studyInfoUID)}</code></span>
                        </div>
                        <div class="detail-row">
                            <span class="label">Started:</span>
                            <span class="value">${startTime}</span>
                        </div>
                        <div class="detail-row">
                            <span class="label">Ended:</span>
                            <span class="value">${endTime}</span>
                        </div>
                        ${record.fileUID ? `
                        <div class="detail-row">
                            <span class="label">File:</span>
                            <span class="value"><code>${this.escapeHtml(record.fileUID)}</code></span>
                        </div>
                        ` : ''}
                    </div>
                    <div class="record-actions">
                        <button class="btn-action" onclick="app.viewRecordDetails('${this.escapeHtml(record.recordUID)}')">
                            📊 View Details
                        </button>
                        ${record.fileUID ? `
                        <button class="btn-action" onclick="app.downloadRecordFile('${this.escapeHtml(record.fileUID)}')">
                            💾 Download
                        </button>
                        ` : ''}
                        ${record.status === 'running' ? `
                        <button class="btn-action btn-stop" onclick="app.stopRecord('${this.escapeHtml(record.recordUID)}')">
                            ⏹️ Stop
                        </button>
                        ` : ''}
                    </div>
                </div>
            `;
        }).join('');
    }

    async viewRecordDetails(recordUID) {
        try {
            const response = await fetch(`${API_BASE_URL}/info/records/get/${recordUID}`);
            if (response.ok) {
                const record = await response.json();
                alert(`Record Details:\n${JSON.stringify(record, null, 2)}`);
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            alert(`Failed to load record details: ${error.message}`);
        }
    }

    async downloadRecordFile(fileUID) {
        window.open(`${API_BASE_URL}/get/file/binary/${fileUID}`, '_blank');
    }

    async stopRecord(recordUID) {
        if (!confirm(`Stop recording ${recordUID}?`)) return;
        
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/record/${recordUID}/stop`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' }
            });
            
            if (response.ok) {
                const result = await response.json();
                alert(`Recording stopped!\nDuration: ${result.durationInSeconds}s`);
                this.loadRecords(); // Refresh list
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            alert(`Failed to stop recording: ${error.message}`);
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

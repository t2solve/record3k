// Live Bug Tracker 3000 - Main Application Logic
// Use relative /api path so all requests go through Caddy's reverse proxy.
// Override at runtime via Settings by storing a custom URL in localStorage.
const API_BASE_URL = localStorage.getItem('api_base_url') || '/api';

// API Token - load from localStorage or use default for development
const API_TOKEN = localStorage.getItem('api_token') || 'default-dev-token';

// Helper to get headers with auth token
function getAuthHeaders() {
    return {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${API_TOKEN}`
    };
}

class BugTrackerApp {
    constructor() {
        this.cameras = [];
        this.records = [];
        this.files = [];
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
            this.loadCameras();
        } else if (this.currentPage === 'records') {
            document.getElementById('refresh-records-btn')?.addEventListener('click', () => this.loadRecords());
            document.getElementById('refresh-files-btn')?.addEventListener('click', () => this.loadFiles());
            document.getElementById('records-sort')?.addEventListener('change', () => this.renderRecords());
            document.getElementById('files-sort')?.addEventListener('change', () => this.renderFiles());
            this.loadRecords();
            this.loadFiles();
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
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`, {
                headers: getAuthHeaders()
            });
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
            const response = await fetch(`${API_BASE_URL}/info/calibrations/list`, {
                headers: getAuthHeaders()
            });
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
            const response = await fetch(`${API_BASE_URL}/info/calibrations/get/${calibrationUID}`, {
                headers: getAuthHeaders()
            });
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
            const response = await fetch(`${API_BASE_URL}/info/pipelines/list`, {
                headers: getAuthHeaders()
            });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            
            const pipelines = await response.json();
            
            if (pipelines.length === 0) {
                pipelineSelect.innerHTML = '<option value="">No pipelines available</option>';
                return;
            }

            // Populate dropdown (latest first if they have timestamps)
            pipelineSelect.innerHTML = pipelines.map((pipe, idx) => {
                const displayText = pipe.description 
                    ? `${pipe.pipelineUID} - ${pipe.description}`
                    : pipe.pipelineUID;
                return `<option value="${this.escapeHtml(pipe.pipelineUID)}" ${idx === 0 ? 'selected' : ''}>
                    ${this.escapeHtml(displayText)}
                </option>`;
            }).join('');
            
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
            const response = await fetch(`${API_BASE_URL}/info/pipelines/get/${pipelineUID}`, {
                headers: getAuthHeaders()
            });
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
            
            const pipeLineTest = document.getElementById('pipeline-test-mode').checked;
            const recordDesc = document.getElementById('record-desc').value.trim();

            // Validation
            if (!camUID || !calibrationUID || !pipelineUID || !scientificName || !weightInMg || durationInSeconds < 1) {
                throw new Error('Please fill in all required fields and ensure duration is at least 1 second');
            }

            // Warning when debug mode (pipeline test mode) is enabled
            if (pipeLineTest) {
                const estimatedGB = (durationInSeconds / 3600).toFixed(2);
                const estimatedMB = (durationInSeconds / 3600 * 1024).toFixed(0);
                const storageWarning = durationInSeconds > 60 
                    ? `Estimated storage required: ~${estimatedGB} GB (approximately 1 GB per hour)`
                    : `Estimated storage required: ~${estimatedMB} MB`;
                
                const confirmed = confirm(
                    `⚠️ WARNING: Pipeline Test Mode (Debug) is enabled!\n\n` +
                    `This mode saves all raw frames to disk for debugging.\n` +
                    `${storageWarning}\n\n` +
                    `Duration: ${hours}h ${minutes}m ${seconds}s\n\n` +
                    `Do you want to continue with debug mode enabled?`
                );
                
                if (!confirmed) {
                    statusEl.style.display = 'none';
                    startBtn.disabled = false;
                    return;
                }
            }

            // Step 1: Create study meta info to get auto-generated studyInfoUID
            statusEl.textContent = 'Creating study meta info...';
            
            const studyPayload = {
                description: studyDesc,
                individualScientificName: scientificName,
                weightInMg: weightInMg
            };
            console.log('Sending to /add/study_meta_info:', studyPayload);
            
            const studyResponse = await fetch(`${API_BASE_URL}/add/study_meta_info`, {
                method: 'POST',
                headers: getAuthHeaders(),
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
                description: recordDesc,
                pipeLineTest: pipeLineTest
            };
            console.log('Sending to /do/record/start:', recordPayload);
            
            const recordResponse = await fetch(`${API_BASE_URL}/do/camera/record/start/${encodeURIComponent(camUID)}`, {
                method: 'POST',
                headers: getAuthHeaders(),
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
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`, {
                headers: getAuthHeaders()
            });
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
            const response = await fetch(`${API_BASE_URL}/info/cameras/list`, {
                headers: getAuthHeaders()
            });
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
                    <button class="btn-action" onclick="app.testCamera('${this.escapeHtml(camera.camUID)}')">
                        🧪 Test
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

    async testCamera(camUID) {
        if (!confirm(`Test camera ${camUID} connectivity?`)) return;
        
        // Show testing in progress
        this.showNotification('Testing camera...', 'info');
        
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/test/${camUID}`, {
                method: 'POST',
                headers: getAuthHeaders()
            });
            
            if (response.ok) {
                const result = await response.json();
                
                // Show success/failure notification
                if (result.success) {
                    this.showNotification(`✓ Camera ${camUID} is ONLINE and working!`, 'success');
                } else {
                    const errorMsg = result.errorDetail ? `\n${result.errorDetail}` : '';
                    this.showNotification(`✗ Camera ${camUID} is OFFLINE: ${result.testResult}${errorMsg}`, 'error');
                }
                
                // Also show detailed alert
                const status = result.success ? '✓ ONLINE' : '✗ OFFLINE';
                let message = `Camera Test Result\n\n`;
                message += `Camera: ${camUID}\n`;
                message += `Status: ${status}\n`;
                message += `Result: ${result.testResult}`;
                
                // Add error detail if present
                if (result.errorDetail) {
                    message += `\n\nError Details:\n${result.errorDetail}`;
                }
                
                alert(message);
                // Reload cameras to reflect any status changes
                this.loadCameras();
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            this.showNotification(`Failed to test camera: ${error.message}`, 'error');
            alert(`Failed to test camera: ${error.message}`);
        }
    }
    
    showNotification(message, type = 'info') {
        // Create notification element if it doesn't exist
        let notification = document.getElementById('notification');
        if (!notification) {
            notification = document.createElement('div');
            notification.id = 'notification';
            notification.className = 'notification';
            document.body.appendChild(notification);
        }
        
        // Set message and type
        notification.textContent = message;
        notification.className = `notification notification-${type} show`;
        
        // Auto-hide after 5 seconds
        setTimeout(() => {
            notification.classList.remove('show');
        }, 5000);
    }

    async calibrateCamera(camUID) {
        if (!confirm(`Start calibration for camera ${camUID}?`)) return;
        
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/calibrate/${camUID}`, {
                method: 'POST',
                headers: getAuthHeaders(),
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
        if (loadingEl) loadingEl.style.display = 'block';
        if (errorEl) errorEl.style.display = 'none';
        if (recordsListEl) recordsListEl.innerHTML = '';

        try {
            const response = await fetch(`${API_BASE_URL}/info/records/list`, {
                headers: getAuthHeaders()
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            this.records = await response.json();
            console.log('Loaded records:', this.records.length);
            this.renderRecords();
            
        } catch (error) {
            console.error('Failed to load records:', error);
            if (errorEl) {
                errorEl.textContent = `Failed to load records: ${error.message}`;
                errorEl.style.display = 'block';
            }
        } finally {
            if (loadingEl) loadingEl.style.display = 'none';
        }
    }

    renderRecords() {
        const recordsListEl = document.getElementById('records-list');
        
        if (!recordsListEl) return;
        
        if (this.records.length === 0) {
            recordsListEl.innerHTML = '<p class="no-data">No recording sessions found</p>';
            return;
        }

        // Sort records based on selected option
        const sortSelect = document.getElementById('records-sort');
        const sortOrder = sortSelect ? sortSelect.value : 'newest';
        
        const sortedRecords = [...this.records].sort((a, b) => {
            const dateA = new Date(a.datetimeStart || 0);
            const dateB = new Date(b.datetimeStart || 0);
            return sortOrder === 'newest' ? dateB - dateA : dateA - dateB;
        });

        recordsListEl.innerHTML = sortedRecords.map(record => {
            const framesRecorded = record.framesRecorded || 0;
            const actualFPS = record.actualFPS ? record.actualFPS.toFixed(2) : 'N/A';
            const startTime = record.datetimeStart ? new Date(record.datetimeStart).toLocaleString() : 'N/A';
            const endTime = record.datetimeEnd ? new Date(record.datetimeEnd).toLocaleString() : 'In Progress';
            const statusClass = record.status === 'completed' ? 'status-finished' : 
                               record.status === 'recording' ? 'status-running' : 'status-other';
            
            return `
                <div class="record-card ${statusClass}">
                    <div class="record-header">
                        <div class="record-title">
                            <h3>📹 Recording: ${this.escapeHtml(record.recordUID)}</h3>
                            <span class="status-badge ${record.status}">${record.status}</span>
                        </div>
                        <div class="record-meta">
                            <span class="duration">${framesRecorded} frames @ ${actualFPS} FPS</span>
                        </div>
                    </div>
                    <div class="record-details">
                        ${record.pipelineUID ? `
                        <div class="detail-row">
                            <span class="label">Pipeline:</span>
                            <span class="value"><code>${this.escapeHtml(record.pipelineUID)}</code></span>
                        </div>
                        ` : ''}
                        ${record.calibrationUID ? `
                        <div class="detail-row">
                            <span class="label">Calibration:</span>
                            <span class="value"><code>${this.escapeHtml(record.calibrationUID)}</code></span>
                        </div>
                        ` : ''}
                        ${record.studyInfoUID ? `
                        <div class="detail-row">
                            <span class="label">Study:</span>
                            <span class="value"><code>${this.escapeHtml(record.studyInfoUID)}</code></span>
                        </div>
                        ` : ''}
                        <div class="detail-row">
                            <span class="label">Started:</span>
                            <span class="value">${startTime}</span>
                        </div>
                        ${record.datetimeEnd ? `
                        <div class="detail-row">
                            <span class="label">Ended:</span>
                            <span class="value">${endTime}</span>
                        </div>
                        ` : ''}
                    </div>
                    <div class="record-actions">
                        ${(record.status === 'recording' || record.status === 'started') ? `
                        <button class="btn-action btn-stop" onclick="app.stopRecording('${this.escapeHtml(record.recordUID)}')">
                            ⏹️ Stop Recording
                        </button>
                        ` : ''}
                        <button class="btn-action" onclick="app.viewRecordDetails('${this.escapeHtml(record.recordUID)}')">
                            📊 View Details
                        </button>
                    </div>
                </div>
            `;
        }).join('');
    }

    async loadFiles() {
        const loadingEl = document.getElementById('files-loading');
        const errorEl = document.getElementById('files-error');
        const filesListEl = document.getElementById('files-list');

        // Show loading state
        if (loadingEl) loadingEl.style.display = 'block';
        if (errorEl) errorEl.style.display = 'none';
        if (filesListEl) filesListEl.innerHTML = '';

        try {
            const response = await fetch(`${API_BASE_URL}/info/files/list`, {
                headers: getAuthHeaders()
            });
            
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}: ${response.statusText}`);
            }

            this.files = await response.json();
            console.log('Loaded files:', this.files.length);
            this.renderFiles();
            
        } catch (error) {
            console.error('Failed to load files:', error);
            if (errorEl) {
                errorEl.textContent = `Failed to load files: ${error.message}`;
                errorEl.style.display = 'block';
            }
        } finally {
            if (loadingEl) loadingEl.style.display = 'none';
        }
    }

    renderFiles() {
        const filesListEl = document.getElementById('files-list');
        
        if (!filesListEl) return;
        
        if (this.files.length === 0) {
            filesListEl.innerHTML = '<p class="no-data">No files found</p>';
            return;
        }

        // Sort files based on selected option
        const sortSelect = document.getElementById('files-sort');
        const sortOrder = sortSelect ? sortSelect.value : 'newest';
        
        const sortedFiles = [...this.files].sort((a, b) => {
            const dateA = new Date(a.datetimeCreated || 0);
            const dateB = new Date(b.datetimeCreated || 0);
            return sortOrder === 'newest' ? dateB - dateA : dateA - dateB;
        });

        filesListEl.innerHTML = sortedFiles.map(file => {
            const sizeInMB = file.archiveSize ? (file.archiveSize / (1024 * 1024)).toFixed(2) : 'N/A';
            const createdTime = file.datetimeCreated ? new Date(file.datetimeCreated).toLocaleString() : 'N/A';
            
            return `
                <div class="file-card">
                    <div class="file-header">
                        <div class="file-title">
                            <h3>📦 ${this.escapeHtml(file.fileUID)}</h3>
                            <span class="file-size">${sizeInMB} MB</span>
                        </div>
                    </div>
                    <div class="file-details">
                        <div class="detail-row">
                            <span class="label">Archive Path:</span>
                            <span class="value"><code>${this.escapeHtml(file.archivePath)}</code></span>
                        </div>
                        ${file.recordUID ? `
                        <div class="detail-row">
                            <span class="label">Record UID:</span>
                            <span class="value"><code>${this.escapeHtml(file.recordUID)}</code></span>
                        </div>
                        ` : ''}
                        ${file.cameraUID ? `
                        <div class="detail-row">
                            <span class="label">Camera UID:</span>
                            <span class="value"><code>${this.escapeHtml(file.cameraUID)}</code></span>
                        </div>
                        ` : ''}
                        <div class="detail-row">
                            <span class="label">Created:</span>
                            <span class="value">${createdTime}</span>
                        </div>
                    </div>
                    <div class="file-actions">
                        <button class="btn-action" onclick="app.downloadFile('${this.escapeHtml(file.fileUID)}')">
                            💾 Download
                        </button>
                        <button class="btn-action" onclick="app.viewFileDetails('${this.escapeHtml(file.fileUID)}')">
                            📊 Details
                        </button>
                    </div>
                </div>
            `;
        }).join('');
    }

    async viewFileDetails(fileUID) {
        try {
            const response = await fetch(`${API_BASE_URL}/info/files/get/${fileUID}`, {
                headers: getAuthHeaders()
            });
            if (response.ok) {
                const file = await response.json();
                alert(`File Details:\n${JSON.stringify(file, null, 2)}`);
            } else {
                throw new Error(`HTTP ${response.status}`);
            }
        } catch (error) {
            alert(`Failed to load file details: ${error.message}`);
        }
    }

    async downloadFile(fileUID) {
        window.open(`${API_BASE_URL}/files/download/${fileUID}`, '_blank');
    }

    async viewRecordDetails(recordUID) {
        try {
            const response = await fetch(`${API_BASE_URL}/info/records/get/${recordUID}`, {
                headers: getAuthHeaders()
            });
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

    async stopRecording(recordUID) {
        if (!confirm(`Are you sure you want to stop recording ${recordUID}?\n\nThis will end the recording session immediately.`)) {
            return;
        }
        
        try {
            const response = await fetch(`${API_BASE_URL}/do/camera/record/stop/${encodeURIComponent(recordUID)}`, {
                method: 'GET',
                headers: getAuthHeaders()
            });
            
            if (response.ok) {
                const result = await response.json();
                alert(`✓ Recording stopped successfully!\n\nRecord UID: ${result.recordUID}\nStatus: ${result.status}`);
                
                // Refresh the records list
                await this.loadRecords();
            } else {
                const errorText = await response.text();
                throw new Error(`HTTP ${response.status} - ${errorText}`);
            }
        } catch (error) {
            alert(`✗ Failed to stop recording: ${error.message}`);
            console.error('Failed to stop recording:', error);
        }
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

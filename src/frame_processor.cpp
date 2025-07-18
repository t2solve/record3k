#include "frame_processor.h"
#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <chrono>
#include <algorithm>
#include <numeric>

FrameProcessor::FrameProcessor(QObject* parent)
    : QObject(parent)
    , m_processingMode(ProcessingMode::CPU_ONLY)
    , m_maxQueueSize(100)
    , m_processingThread(nullptr)
    , m_outputDirectory("/tmp")
    , m_saveFrames(false)
    , m_statisticsTimer(nullptr)
    , m_cudaInitialized(false)
{
    // Initialize statistics timer
    m_statisticsTimer = new QTimer(this);
    m_statisticsTimer->setInterval(1000); // Update every second
    connect(m_statisticsTimer, &QTimer::timeout, this, &FrameProcessor::updateStatistics);
    
    // Check CUDA availability
#ifdef CUDA_ENABLED
    m_cudaInitialized = initializeCUDA();
    m_stats.cudaAvailable = m_cudaInitialized;
    if (m_cudaInitialized) {
        qDebug() << "CUDA initialized successfully. Device:" << QString::fromStdString(m_cudaDeviceName);
    } else {
        qDebug() << "CUDA initialization failed or not available";
    }
#else
    qDebug() << "CUDA support not compiled in";
#endif
}

FrameProcessor::~FrameProcessor() {
    stop();
    
#ifdef CUDA_ENABLED
    cleanupCUDA();
#endif
}

void FrameProcessor::setProcessingMode(ProcessingMode mode) {
    QMutexLocker locker(&m_queueMutex);
    
    // Validate mode based on CUDA availability
    if (mode == ProcessingMode::CUDA_ONLY && !m_stats.cudaAvailable) {
        qWarning() << "CUDA_ONLY mode requested but CUDA not available, falling back to CPU_ONLY";
        m_processingMode = ProcessingMode::CPU_ONLY;
    } else if (mode == ProcessingMode::CUDA_PREFERRED && !m_stats.cudaAvailable) {
        qWarning() << "CUDA_PREFERRED mode requested but CUDA not available, falling back to CPU_ONLY";
        m_processingMode = ProcessingMode::CPU_ONLY;
    } else {
        m_processingMode = mode;
    }
    
    qDebug() << "Processing mode set to:" << static_cast<int>(m_processingMode);
}

void FrameProcessor::setMaxQueueSize(int maxSize) {
    QMutexLocker locker(&m_queueMutex);
    m_maxQueueSize = std::max(1, maxSize);
    qDebug() << "Max queue size set to:" << m_maxQueueSize;
}

void FrameProcessor::setOutputDirectory(const std::string& dir) {
    QMutexLocker locker(&m_queueMutex);
    m_outputDirectory = dir;
    
    // Create directory if it doesn't exist
    QDir outputDir(QString::fromStdString(dir));
    if (!outputDir.exists()) {
        if (outputDir.mkpath(".")) {
            qDebug() << "Created output directory:" << QString::fromStdString(dir);
        } else {
            qWarning() << "Failed to create output directory:" << QString::fromStdString(dir);
        }
    }
}

void FrameProcessor::setSaveFrames(bool save) {
    QMutexLocker locker(&m_queueMutex);
    m_saveFrames = save;
    qDebug() << "Save frames:" << save;
}

void FrameProcessor::setFilterChain(const std::vector<FilterConfig>& filters) {
    QMutexLocker locker(&m_queueMutex);
    m_filterChain = filters;
    qDebug() << "Filter chain updated with" << filters.size() << "filters";
}

void FrameProcessor::start() {
    if (m_running) {
        qDebug() << "Frame processor already running";
        return;
    }
    
    qDebug() << "Starting frame processor";
    
    m_running = true;
    m_paused = false;
    m_stopping = false;
    
    // Create processing thread
    m_processingThread = new QThread(this);
    
    // Move processing to thread
    QObject::connect(m_processingThread, &QThread::started, this, &FrameProcessor::processFrames);
    QObject::connect(m_processingThread, &QThread::finished, m_processingThread, &QObject::deleteLater);
    
    // Start thread and statistics timer
    m_processingThread->start();
    m_statisticsTimer->start();
    
    qDebug() << "Frame processor started";
}

void FrameProcessor::stop() {
    if (!m_running) {
        return;
    }
    
    qDebug() << "Stopping frame processor";
    
    m_stopping = true;
    m_running = false;
    m_paused = false;
    
    // Wake up processing thread
    m_queueCondition.wakeAll();
    
    // Stop statistics timer
    if (m_statisticsTimer) {
        m_statisticsTimer->stop();
    }
    
    // Wait for thread to finish
    if (m_processingThread) {
        m_processingThread->quit();
        if (!m_processingThread->wait(5000)) {
            qWarning() << "Processing thread did not finish within timeout, terminating";
            m_processingThread->terminate();
            m_processingThread->wait();
        }
        m_processingThread = nullptr;
    }
    
    qDebug() << "Frame processor stopped";
}

void FrameProcessor::pause() {
    if (!m_running) {
        return;
    }
    
    m_paused = true;
    qDebug() << "Frame processor paused";
}

void FrameProcessor::resume() {
    if (!m_running) {
        return;
    }
    
    m_paused = false;
    m_queueCondition.wakeAll();
    qDebug() << "Frame processor resumed";
}

bool FrameProcessor::enqueueFrame(const ProcessingFrame& frame) {
    QMutexLocker locker(&m_queueMutex);
    
    if (!m_running) {
        return false;
    }
    
    // Check queue size
    if (m_frameQueue.size() >= m_maxQueueSize) {
        // Drop oldest frame
        m_frameQueue.dequeue();
        m_stats.droppedFrames++;
        qDebug() << "Frame queue full, dropped frame" << frame.frameId;
    }
    
    // Add new frame
    m_frameQueue.enqueue(frame);
    m_stats.totalFrames++;
    
    // Wake up processing thread
    m_queueCondition.wakeOne();
    
    return true;
}

void FrameProcessor::clearQueue() {
    QMutexLocker locker(&m_queueMutex);
    m_frameQueue.clear();
    qDebug() << "Frame queue cleared";
}

int FrameProcessor::getQueueSize() const {
    QMutexLocker locker(&m_queueMutex);
    return m_frameQueue.size();
}

void FrameProcessor::resetStatistics() {
    QMutexLocker locker(&m_statsMutex);
    m_stats.totalFrames = 0;
    m_stats.processedFrames = 0;
    m_stats.droppedFrames = 0;
    m_stats.averageProcessingTime = 0.0;
    m_processingTimes.clear();
    qDebug() << "Statistics reset";
}

void FrameProcessor::processFrames() {
    qDebug() << "Frame processing thread started";
    
    while (m_running) {
        ProcessingFrame frame;
        bool hasFrame = false;
        
        // Get frame from queue
        {
            QMutexLocker locker(&m_queueMutex);
            
            // Wait for frame or stop signal
            while (m_frameQueue.isEmpty() && m_running && !m_stopping) {
                m_queueCondition.wait(&m_queueMutex);
            }
            
            if (!m_running || m_stopping) {
                break;
            }
            
            if (!m_frameQueue.isEmpty()) {
                frame = m_frameQueue.dequeue();
                hasFrame = true;
            }
        }
        
        if (!hasFrame) {
            continue;
        }
        
        // Check if paused
        while (m_paused && m_running) {
            QThread::msleep(10);
        }
        
        if (!m_running) {
            break;
        }
        
        // Process frame
        try {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            cv::Mat processedFrame = processFrame(frame.image);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();
            
            // Update processing time
            updateProcessingTime(duration);
            
            // Save frame if requested
            if (m_saveFrames && !processedFrame.empty()) {
                std::string filename = generateFilename(frame.frameId, frame.timestamp);
                if (saveFrame(processedFrame, filename)) {
                    qDebug() << "Saved processed frame:" << QString::fromStdString(filename);
                }
            }
            
            // Create result frame
            ProcessingFrame result(processedFrame, frame.frameId, frame.timestamp, frame.metadata);
            
            // Emit signal
            emit frameProcessed(result);
            
            m_stats.processedFrames++;
            
        } catch (const std::exception& e) {
            qWarning() << "Error processing frame" << frame.frameId << ":" << e.what();
            emit processingError(QString("Frame processing error: %1").arg(e.what()));
        }
    }
    
    qDebug() << "Frame processing thread finished";
}

cv::Mat FrameProcessor::processFrame(const cv::Mat& input) {
    if (input.empty()) {
        return cv::Mat();
    }
    
    cv::Mat result;
    
    // Choose processing method based on mode
    switch (m_processingMode) {
        case ProcessingMode::CPU_ONLY:
            result = processFrameCPU(input, m_filterChain);
            break;
            
        case ProcessingMode::CUDA_PREFERRED:
#ifdef CUDA_ENABLED
            if (m_cudaInitialized) {
                try {
                    result = processFrameCUDA(input, m_filterChain);
                    break;
                } catch (const std::exception& e) {
                    qWarning() << "CUDA processing failed, falling back to CPU:" << e.what();
                }
            }
#endif
            // Fall back to CPU
            result = processFrameCPU(input, m_filterChain);
            break;
            
        case ProcessingMode::CUDA_ONLY:
#ifdef CUDA_ENABLED
            if (m_cudaInitialized) {
                result = processFrameCUDA(input, m_filterChain);
            } else {
                throw std::runtime_error("CUDA processing requested but not available");
            }
#else
            throw std::runtime_error("CUDA processing requested but not compiled in");
#endif
            break;
    }
    
    return result;
}

cv::Mat FrameProcessor::processFrameCPU(const cv::Mat& input, const std::vector<FilterConfig>& filters) {
    cv::Mat result = input.clone();
    
    for (const auto& filter : filters) {
        if (filter.type == FilterType::NONE) {
            continue;
        }
        
        cv::Mat filtered = applyCPUFilter(result, filter);
        if (!filtered.empty()) {
            result = filtered;
        }
    }
    
    return result;
}

cv::Mat FrameProcessor::applyCPUFilter(const cv::Mat& input, const FilterConfig& config) {
    switch (config.type) {
        case FilterType::GAUSSIAN_BLUR:
            return applyGaussianBlurCPU(input, config);
        case FilterType::BILATERAL_FILTER:
            return applyBilateralFilterCPU(input, config);
        case FilterType::MEDIAN_FILTER:
            return applyMedianFilterCPU(input, config);
        case FilterType::EDGE_DETECTION:
            return applyEdgeDetectionCPU(input, config);
        case FilterType::SHARPEN:
            return applySharpenCPU(input, config);
        case FilterType::DENOISE:
            return applyDenoiseCPU(input, config);
        default:
            return input.clone();
    }
}

cv::Mat FrameProcessor::applyGaussianBlurCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result;
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    double sigmaX = config.getParameter("sigma_x", 1.0);
    double sigmaY = config.getParameter("sigma_y", sigmaX);
    
    // Ensure kernel size is odd
    if (kernelSize % 2 == 0) {
        kernelSize++;
    }
    
    cv::GaussianBlur(input, result, cv::Size(kernelSize, kernelSize), sigmaX, sigmaY);
    return result;
}

cv::Mat FrameProcessor::applyBilateralFilterCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result;
    int d = static_cast<int>(config.getParameter("d", 9));
    double sigmaColor = config.getParameter("sigma_color", 75.0);
    double sigmaSpace = config.getParameter("sigma_space", 75.0);
    
    cv::bilateralFilter(input, result, d, sigmaColor, sigmaSpace);
    return result;
}

cv::Mat FrameProcessor::applyMedianFilterCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result;
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    
    // Ensure kernel size is odd
    if (kernelSize % 2 == 0) {
        kernelSize++;
    }
    
    cv::medianBlur(input, result, kernelSize);
    return result;
}

cv::Mat FrameProcessor::applyEdgeDetectionCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result, gray;
    double threshold1 = config.getParameter("threshold1", 100.0);
    double threshold2 = config.getParameter("threshold2", 200.0);
    int apertureSize = static_cast<int>(config.getParameter("aperture_size", 3));
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = input;
    }
    
    cv::Canny(gray, result, threshold1, threshold2, apertureSize);
    return result;
}

cv::Mat FrameProcessor::applySharpenCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result;
    double strength = config.getParameter("strength", 1.0);
    
    // Sharpening kernel
    cv::Mat kernel = (cv::Mat_<float>(3, 3) << 
        0, -1, 0,
        -1, 5, -1,
        0, -1, 0);
    
    // Apply with strength
    kernel = kernel * strength;
    kernel.at<float>(1, 1) = 1 + 4 * strength;
    
    cv::filter2D(input, result, -1, kernel);
    return result;
}

cv::Mat FrameProcessor::applyDenoiseCPU(const cv::Mat& input, const FilterConfig& config) {
    cv::Mat result;
    double h = config.getParameter("h", 10.0);
    double hColor = config.getParameter("h_color", 10.0);
    int templateWindowSize = static_cast<int>(config.getParameter("template_window_size", 7));
    int searchWindowSize = static_cast<int>(config.getParameter("search_window_size", 21));
    
    if (input.channels() == 1) {
        cv::fastNlMeansDenoising(input, result, h, templateWindowSize, searchWindowSize);
    } else {
        cv::fastNlMeansDenoisingColored(input, result, h, hColor, templateWindowSize, searchWindowSize);
    }
    
    return result;
}

#ifdef CUDA_ENABLED
bool FrameProcessor::initializeCUDA() {
    try {
        int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
        if (deviceCount == 0) {
            qDebug() << "No CUDA devices found";
            return false;
        }
        
        // Get device info
        cv::cuda::DeviceInfo deviceInfo;
        m_cudaDeviceName = deviceInfo.name();
        
        qDebug() << "CUDA device count:" << deviceCount;
        qDebug() << "Using device:" << QString::fromStdString(m_cudaDeviceName);
        
        // Test basic CUDA functionality
        cv::cuda::GpuMat testMat;
        testMat.create(100, 100, CV_8UC3);
        
        return true;
    } catch (const std::exception& e) {
        qWarning() << "CUDA initialization failed:" << e.what();
        return false;
    }
}

void FrameProcessor::cleanupCUDA() {
    // Cleanup CUDA resources
    m_gpuFrame.release();
    m_gpuResult.release();
    
    if (m_gaussianFilter) {
        m_gaussianFilter.release();
    }
    if (m_bilateralFilter) {
        m_bilateralFilter.release();
    }
    if (m_medianFilter) {
        m_medianFilter.release();
    }
}

cv::Mat FrameProcessor::processFrameCUDA(const cv::Mat& input, const std::vector<FilterConfig>& filters) {
    // Upload to GPU
    m_gpuFrame.upload(input);
    cv::cuda::GpuMat gpuResult = m_gpuFrame.clone();
    
    for (const auto& filter : filters) {
        if (filter.type == FilterType::NONE) {
            continue;
        }
        
        cv::Mat filtered = applyCUDAFilter(gpuResult, filter);
        if (!filtered.empty()) {
            gpuResult.upload(filtered);
        }
    }
    
    // Download result
    cv::Mat result;
    gpuResult.download(result);
    return result;
}

cv::Mat FrameProcessor::applyCUDAFilter(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    cv::Mat result;
    
    switch (config.type) {
        case FilterType::GAUSSIAN_BLUR:
            result = applyGaussianBlurCUDA(input, config);
            break;
        case FilterType::BILATERAL_FILTER:
            result = applyBilateralFilterCUDA(input, config);
            break;
        case FilterType::MEDIAN_FILTER:
            result = applyMedianFilterCUDA(input, config);
            break;
        case FilterType::EDGE_DETECTION:
            result = applyEdgeDetectionCUDA(input, config);
            break;
        case FilterType::SHARPEN:
            result = applySharpenCUDA(input, config);
            break;
        case FilterType::DENOISE:
            result = applyDenoiseCUDA(input, config);
            break;
        default:
            input.download(result);
            break;
    }
    
    return result;
}

cv::Mat FrameProcessor::applyGaussianBlurCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    cv::Mat result;
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    double sigmaX = config.getParameter("sigma_x", 1.0);
    double sigmaY = config.getParameter("sigma_y", sigmaX);
    
    // Ensure kernel size is odd
    if (kernelSize % 2 == 0) {
        kernelSize++;
    }
    
    cv::cuda::GpuMat gpuResult;
    
    // Create or reuse filter
    if (!m_gaussianFilter || 
        m_gaussianFilter->ksize != cv::Size(kernelSize, kernelSize)) {
        m_gaussianFilter = cv::cuda::createGaussianFilter(
            input.type(), -1, cv::Size(kernelSize, kernelSize), sigmaX, sigmaY);
    }
    
    m_gaussianFilter->apply(input, gpuResult);
    gpuResult.download(result);
    return result;
}

cv::Mat FrameProcessor::applyBilateralFilterCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    cv::Mat result;
    int kernelSize = static_cast<int>(config.getParameter("kernel_size", 5));
    float sigmaColor = static_cast<float>(config.getParameter("sigma_color", 50.0));
    float sigmaSpace = static_cast<float>(config.getParameter("sigma_space", 50.0));
    
    cv::cuda::GpuMat gpuResult;
    cv::cuda::bilateralFilter(input, gpuResult, kernelSize, sigmaColor, sigmaSpace);
    gpuResult.download(result);
    return result;
}

cv::Mat FrameProcessor::applyMedianFilterCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    // Note: CUDA doesn't have direct median filter, fall back to CPU
    cv::Mat cpuInput, result;
    input.download(cpuInput);
    return applyMedianFilterCPU(cpuInput, config);
}

cv::Mat FrameProcessor::applyEdgeDetectionCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    cv::Mat result;
    double threshold1 = config.getParameter("threshold1", 100.0);
    double threshold2 = config.getParameter("threshold2", 200.0);
    int apertureSize = static_cast<int>(config.getParameter("aperture_size", 3));
    
    cv::cuda::GpuMat gpuGray, gpuResult;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cuda::cvtColor(input, gpuGray, cv::COLOR_BGR2GRAY);
    } else {
        gpuGray = input;
    }
    
    cv::cuda::Canny(gpuGray, gpuResult, threshold1, threshold2, apertureSize);
    gpuResult.download(result);
    return result;
}

cv::Mat FrameProcessor::applySharpenCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    cv::Mat result;
    double strength = config.getParameter("strength", 1.0);
    
    // Sharpening kernel
    cv::Mat kernel = (cv::Mat_<float>(3, 3) << 
        0, -1, 0,
        -1, 5, -1,
        0, -1, 0);
    
    kernel = kernel * strength;
    kernel.at<float>(1, 1) = 1 + 4 * strength;
    
    cv::cuda::GpuMat gpuResult;
    cv::Ptr<cv::cuda::Filter> filter = cv::cuda::createLinearFilter(input.type(), -1, kernel);
    filter->apply(input, gpuResult);
    gpuResult.download(result);
    return result;
}

cv::Mat FrameProcessor::applyDenoiseCUDA(const cv::cuda::GpuMat& input, const FilterConfig& config) {
    // Note: CUDA doesn't have direct NLM denoising, fall back to CPU
    cv::Mat cpuInput, result;
    input.download(cpuInput);
    return applyDenoiseCPU(cpuInput, config);
}
#endif

bool FrameProcessor::saveFrame(const cv::Mat& frame, const std::string& filename) {
    if (frame.empty()) {
        return false;
    }
    
    try {
        std::string fullPath = m_outputDirectory + "/" + filename;
        
        // Set JPEG quality
        std::vector<int> compression_params;
        compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
        compression_params.push_back(90);
        
        bool success = cv::imwrite(fullPath, frame, compression_params);
        if (!success) {
            qWarning() << "Failed to save frame to:" << QString::fromStdString(fullPath);
        }
        return success;
    } catch (const std::exception& e) {
        qWarning() << "Exception saving frame:" << e.what();
        return false;
    }
}

std::string FrameProcessor::generateFilename(VmbUint64_t frameId, double timestamp) {
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp * 1000));
    QString timestampStr = dateTime.toString("yyyy-MM-dd_hh-mm-ss-zzz");
    return QString("frame_%1_%2.jpg").arg(frameId).arg(timestampStr).toStdString();
}

void FrameProcessor::updateProcessingTime(double processingTime) {
    QMutexLocker locker(&m_statsMutex);
    m_processingTimes.push_back(processingTime);
    
    // Keep only last 100 measurements
    if (m_processingTimes.size() > 100) {
        m_processingTimes.erase(m_processingTimes.begin());
    }
    
    // Calculate average
    double sum = std::accumulate(m_processingTimes.begin(), m_processingTimes.end(), 0.0);
    m_stats.averageProcessingTime = sum / m_processingTimes.size();
}

void FrameProcessor::updateStatistics() {
    emit statisticsUpdated();
}

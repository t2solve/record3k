#ifndef FRAME_PROCESSOR_H
#define FRAME_PROCESSOR_H

#include <VmbCPP/VmbCPP.h>
#include <VmbCPP/Frame.h>
#include <VmbCPP/Camera.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>

#ifdef CUDA_ENABLED
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafeatures2d.hpp>
#include <opencv2/cudafilters.hpp>
#endif

#include <QObject>
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QDebug>
#include <QTimer>

#include <memory>
#include <atomic>
#include <string>
#include <map>
#include <vector>

using namespace VmbCPP;

// Processing modes
enum class ProcessingMode {
    CPU_ONLY,
    CUDA_PREFERRED,
    CUDA_ONLY
};

// Filter types
enum class FilterType {
    NONE,
    GAUSSIAN_BLUR,
    BILATERAL_FILTER,
    MEDIAN_FILTER,
    EDGE_DETECTION,
    SHARPEN,
    DENOISE,
    CUSTOM
};

// Frame processing data structure
struct ProcessingFrame {
    cv::Mat image;
    VmbUint64_t frameId;
    double timestamp;
    std::string metadata;
    
    ProcessingFrame() : frameId(0), timestamp(0.0) {}
    ProcessingFrame(const cv::Mat& img, VmbUint64_t id, double ts, const std::string& meta = "")
        : image(img.clone()), frameId(id), timestamp(ts), metadata(meta) {}
};

// Filter configuration - holds parameters for any processing step
struct FilterConfig {
    std::map<std::string, double> parameters;
    
    FilterConfig() = default;
    
    void setParameter(const std::string& key, double value) {
        parameters[key] = value;
    }
    
    double getParameter(const std::string& key, double defaultValue = 0.0) const {
        auto it = parameters.find(key);
        return (it != parameters.end()) ? it->second : defaultValue;
    }
    
    bool hasParameter(const std::string& key) const {
        return parameters.find(key) != parameters.end();
    }
};

// Base interface for processing steps
class ProcessStep {
public:
    virtual ~ProcessStep() = default;
    
    // Process a frame with given configuration
    virtual cv::Mat process(const cv::Mat& input, const FilterConfig& config) = 0;
    
    // Get the name/type of this processing step
    virtual std::string getName() const = 0;
    
    // Check if this step is available (e.g., CUDA might not be available)
    virtual bool isAvailable() const = 0;
    
    // Get processing mode (CPU or CUDA)
    virtual ProcessingMode getMode() const = 0;
};

// CPU-based processing step
class CPUProcessStep : public ProcessStep {
public:
    CPUProcessStep(FilterType filterType) : m_filterType(filterType) {}
    
    cv::Mat process(const cv::Mat& input, const FilterConfig& config) override;
    std::string getName() const override;
    bool isAvailable() const override { return true; } // CPU always available
    ProcessingMode getMode() const override { return ProcessingMode::CPU_ONLY; }
    
private:
    FilterType m_filterType;
    
    // Individual filter implementations
    cv::Mat applyGaussianBlur(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyBilateralFilter(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyMedianFilter(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyEdgeDetection(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applySharpen(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyDenoise(const cv::Mat& input, const FilterConfig& config);
};

#ifdef CUDA_ENABLED
// CUDA-based processing step
class CUDAProcessStep : public ProcessStep {
public:
    CUDAProcessStep(FilterType filterType);
    ~CUDAProcessStep();
    
    cv::Mat process(const cv::Mat& input, const FilterConfig& config) override;
    std::string getName() const override;
    bool isAvailable() const override { return m_cudaInitialized; }
    ProcessingMode getMode() const override { return ProcessingMode::CUDA_ONLY; }
    
private:
    FilterType m_filterType;
    bool m_cudaInitialized;
    
    // CUDA memory management
    cv::cuda::GpuMat m_gpuInput;
    cv::cuda::GpuMat m_gpuOutput;
    
    // Reusable CUDA filter objects for efficiency
    cv::Ptr<cv::cuda::Filter> m_gaussianFilter;
    cv::Ptr<cv::cuda::Filter> m_bilateralFilter;
    
    // Individual filter implementations
    cv::Mat applyGaussianBlur(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyBilateralFilter(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyMedianFilter(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyEdgeDetection(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applySharpen(const cv::Mat& input, const FilterConfig& config);
    cv::Mat applyDenoise(const cv::Mat& input, const FilterConfig& config);
    
    bool initializeCUDA();
    void cleanupCUDA();
};
#endif

// Processing step with configuration
struct ProcessingStep {
    std::unique_ptr<ProcessStep> step;
    FilterConfig config;
    
    ProcessingStep(std::unique_ptr<ProcessStep> s, const FilterConfig& c = FilterConfig()) 
        : step(std::move(s)), config(c) {}
    
    // Move constructor and assignment
    ProcessingStep(ProcessingStep&& other) = default;
    ProcessingStep& operator=(ProcessingStep&& other) = default;
    
    // Delete copy constructor and assignment
    ProcessingStep(const ProcessingStep&) = delete;
    ProcessingStep& operator=(const ProcessingStep&) = delete;
};

// Frame processor class
class FrameProcessor : public QObject {
    Q_OBJECT

public:
    explicit FrameProcessor(QObject* parent = nullptr);
    ~FrameProcessor();

    // Configuration
    void setProcessingMode(ProcessingMode mode);
    void setMaxQueueSize(int maxSize);
    void setOutputDirectory(const std::string& dir);
    void setSaveFrames(bool save);
    void setProcessingSteps(std::vector<ProcessingStep> steps);
    
    // Helper method to create processing steps
    static std::unique_ptr<ProcessStep> createProcessStep(FilterType filterType, ProcessingMode preferredMode = ProcessingMode::CUDA_PREFERRED);
    
    // Convenience method to add a processing step
    void addProcessingStep(FilterType filterType, const FilterConfig& config = FilterConfig(), ProcessingMode preferredMode = ProcessingMode::CUDA_PREFERRED);
    
    // Processing control
    void start();
    void stop();
    void pause();
    void resume();
    
    // Queue management
    bool enqueueFrame(const ProcessingFrame& frame);
    void clearQueue();
    int getQueueSize() const;
    
    // Statistics
    struct Statistics {
        std::atomic<uint64_t> totalFrames{0};
        std::atomic<uint64_t> processedFrames{0};
        std::atomic<uint64_t> droppedFrames{0};
        std::atomic<double> averageProcessingTime{0.0};
        std::atomic<bool> cudaAvailable{false};
    };
    
    const Statistics& getStatistics() const { return m_stats; }
    void resetStatistics();

signals:
    void frameProcessed(const ProcessingFrame& frame);
    void processingError(const QString& error);
    void statisticsUpdated();

private slots:
    void processFrames();
    void updateStatistics();

private:
    // Core processing functions
    cv::Mat processFrame(const cv::Mat& input);
    
    // Utility functions
    bool saveFrame(const cv::Mat& frame, const std::string& filename);
    std::string generateFilename(VmbUint64_t frameId, double timestamp);
    void updateProcessingTime(double processingTime);
    
    // Member variables
    ProcessingMode m_processingMode;
    std::vector<ProcessingStep> m_processingSteps;
    
    // Queue management
    QQueue<ProcessingFrame> m_frameQueue;
    mutable QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    int m_maxQueueSize;
    
    // Threading
    QThread* m_processingThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_stopping{false};
    
    // Output configuration
    std::string m_outputDirectory;
    bool m_saveFrames;
    
    // Statistics
    Statistics m_stats;
    QTimer* m_statisticsTimer;
    QMutex m_statsMutex;
    std::vector<double> m_processingTimes;
};

#endif // FRAME_PROCESSOR_H

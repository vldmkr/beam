// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "external_pow.h"
#include "utility/helpers.h"
#include <boost/dll.hpp>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <sstream>
#include "3rdparty/crypto/equihash.h"

#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wignored-attributes"
#endif

#include <CL/cl.hpp>

#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

using namespace std;

namespace
{
    class minerBridge {
    public:
        virtual ~minerBridge() = default;

        virtual bool hasWork() = 0;
        virtual void getWork(int64_t*, uint64_t*, uint8_t*, uint32_t*) = 0;

        virtual void handleSolution(int64_t&, uint64_t&, std::vector<uint32_t>&, uint32_t) = 0;
    };

    struct Job 
    {
        string jobID;
        beam::Merkle::Hash input;
        beam::Block::PoW pow;
        beam::IExternalPOW::BlockFound callback;
    };

    using SolutionCallback = function<void(Job&& job)>;

    class WorkProvider : public minerBridge
    {
    public:
        WorkProvider(SolutionCallback&& solutionCallback)
            : _solutionCallback(move(solutionCallback))
            , _stopped(false)
        {
            ECC::GenRandom(&_nonce, 8);
        }
        virtual ~WorkProvider()
        {

        }

        void feedJob(const Job& job)
        {
            unique_lock<mutex> guard(_mutex);
            _input.assign(job.input.m_pData, job.input.m_pData + job.input.nBytes);
            _workID = stoll(job.jobID);
            _difficulty = job.pow.m_Difficulty.m_Packed;
        }

        void stop()
        {
            unique_lock<mutex> guard(_mutex);
            _stopped = true;
        }

        bool hasWork() override
        {
            unique_lock<mutex> guard(_mutex);
            return !_input.empty();
        }

        void getWork(int64_t* workOut, uint64_t* nonceOut, uint8_t* dataOut, uint32_t* difficulty) override
        {
            unique_lock<mutex> guard(_mutex);
            *workOut = _workID;
            *nonceOut = _nonce.fetch_add(1);
            *difficulty = _difficulty;
            copy_n(&_input[0], _input.size(), dataOut);
        }

        void handleSolution(int64_t &workId, uint64_t &nonce, vector<uint32_t> &indices, uint32_t difficulty) override
        {
            Job job;
            auto compressed = GetMinimalFromIndices(indices, 25);
            copy(compressed.begin(), compressed.end(), job.pow.m_Indices.begin());
            beam::Block::PoW::NonceType t((const uint8_t*)&nonce);
            job.pow.m_Nonce = t;
            job.pow.m_Difficulty = beam::Difficulty(difficulty);
            job.jobID = to_string(workId);
            _solutionCallback(move(job));
        }

    private:
        SolutionCallback _solutionCallback;
        bool _stopped;
        vector<uint8_t> _input;
        atomic<uint64_t> _nonce;
        uint32_t _difficulty;
        int64_t _workID = 0;
        mutable mutex _mutex;
    };

    using std::vector;

    struct clCallbackData {
        void* host;
        uint32_t gpuIndex;
        int64_t workId;
        uint64_t nonce;
        uint32_t difficulty;
    };

    class clHost {
    private:
        // OpenCL
        vector<cl::Platform> platforms;
        vector<cl::Context> contexts;
        vector<cl::CommandQueue> queues;
        vector<cl::Device> devices;
        vector<cl::Event> events;
        vector<unsigned*> results;

        vector< vector<cl::Buffer> > buffers;
        vector< vector<cl::Kernel> > kernels;

        // Statistics
        vector<int> solutionCnt;

        // To check if a mining thread stoped and we must resume it
        vector<bool> paused;

        // Callback data
        vector<clCallbackData> currentWork;
        std::atomic<bool> restart = true;

        // Functions
        void detectPlatFormDevices(vector<int32_t>, bool);
        void loadAndCompileKernel(cl::Device &, uint32_t);
        void queueKernels(uint32_t, clCallbackData*);

        // The connector
        minerBridge* bridge;

    public:

        void setup(minerBridge*, vector<int32_t>, bool);
        void startMining();
        void stopMining();
        void callbackFunc(cl_int, void*);
        ~clHost();
    };

    // Helper functions to split a string
    inline vector<string> &split(const string &s, char delim, vector<string> &elems) {
        stringstream ss(s);
        string item;
        while(getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }


    inline vector<string> split(const string &s, char delim) {
        vector<string> elems;
        return split(s, delim, elems);
    }


    // Helper function that tests if a OpenCL device supports a certain CL extension
    inline bool hasExtension(cl::Device &device, string extension) {
        string info;
        device.getInfo(CL_DEVICE_EXTENSIONS, &info);
        vector<string> extens = split(info, ' ');

        for (uint32_t i=0; i<extens.size(); i++) {
            if (extens[i].compare(extension) == 0) 	return true;
        }
        return false;
    }


    // This is a bit ugly c-style, but the OpenCL headers are initially for c and
    // support c-style callback functions (no member functions) only.
    // This function will be called every time a GPU is done with its current work
    void CL_CALLBACK CCallbackFunc(cl_event ev, cl_int err , void* data) {
        clHost* self = static_cast<clHost*>(((clCallbackData*) data)->host);
        self->callbackFunc(err,data);
    }


    // Function to load the OpenCL kernel and prepare our device for mining
    void clHost::loadAndCompileKernel(cl::Device &device, uint32_t pl) {
        // reading the kernel file
        cl_int err;

        static const char equihash_150_5_cl[] =
        {
    #include "equihash_150_5.dat"
        , '\0'
        };

        cl::Program::Sources source(1,std::make_pair(equihash_150_5_cl, sizeof(equihash_150_5_cl)));

        // Create a program object and build it
        vector<cl::Device> devicesTMP;
        devicesTMP.push_back(device);

        cl::Program program(contexts[pl], source);
        err = program.build(devicesTMP,"");

        // Check if the build was Ok
        if (!err) {
            // Store the device and create a queue for it
            cl_command_queue_properties queue_prop = 0;
            devices.push_back(device);
            queues.push_back(cl::CommandQueue(contexts[pl], devices[devices.size()-1], queue_prop, NULL));

            // Reserve events, space for storing results and so on
            events.push_back(cl::Event());
            results.push_back(NULL);
            currentWork.push_back(clCallbackData());
            paused.push_back(true);
            solutionCnt.push_back(0);

            // Create the kernels
            vector<cl::Kernel> newKernels;
            newKernels.push_back(cl::Kernel(program, "clearCounter", &err));
            newKernels.push_back(cl::Kernel(program, "round0", &err));
            newKernels.push_back(cl::Kernel(program, "round1", &err));
            newKernels.push_back(cl::Kernel(program, "round2", &err));
            newKernels.push_back(cl::Kernel(program, "round3", &err));
            newKernels.push_back(cl::Kernel(program, "round4", &err));
            newKernels.push_back(cl::Kernel(program, "round5", &err));
            newKernels.push_back(cl::Kernel(program, "combine", &err));
            kernels.push_back(newKernels);

            // Create the buffers
            vector<cl::Buffer> newBuffers;
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 71303168, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint2) * 71303168, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint4) * 256, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint) * 49152, NULL, &err));
            newBuffers.push_back(cl::Buffer(contexts[pl], CL_MEM_READ_WRITE,  sizeof(cl_uint) * 324, NULL, &err));
            buffers.push_back(newBuffers);

        } else {
            // TODO report
        }
    }


    // Detect the OpenCL hardware on this system
    void clHost::detectPlatFormDevices(vector<int32_t> selDev, bool allowCPU) {
        // read the OpenCL platforms on this system
        cl::Platform::get(&platforms);

        // this is for enumerating the devices
        int32_t curDiv = 0;
        uint32_t selNum = 0;

        for (uint32_t pl=0; pl<platforms.size(); pl++) {
            // Create the OpenCL contexts, one for each platform
            cl_context_properties properties[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platforms[pl](), 0};
            cl::Context context;
            if (allowCPU) {
                context = cl::Context(CL_DEVICE_TYPE_ALL, properties);
            } else {
                context = cl::Context(CL_DEVICE_TYPE_GPU, properties);
            }
            contexts.push_back(context);

            // Read the devices of this platform
            vector< cl::Device > nDev = context.getInfo<CL_CONTEXT_DEVICES>();
            for (uint32_t di=0; di<nDev.size(); di++) {

                // Print the device name
                string name;
                if ( hasExtension(nDev[di], "cl_amd_device_attribute_query") ) {
                    nDev[di].getInfo(0x4038,&name);			// on AMD this gives more readable result
                } else {
                    nDev[di].getInfo(CL_DEVICE_NAME, &name); 	// for all other GPUs
                }

                // Get rid of strange characters at the end of device name
                if (isalnum((int) name.back()) == 0) {
                    name.pop_back();
                }

                // Check if the device should be selected
                bool pick = false;
                if (selDev[0] == -1) pick = true;
                if (selNum < selDev.size()) {
                    if (curDiv == selDev[selNum]) {
                        pick = true;
                        selNum++;
                    }
                }


                if (pick) {
                    // Check if the CPU / GPU has enough memory
                    uint64_t deviceMemory = nDev[di].getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                    uint64_t needed = 7* ((uint64_t) 570425344) + 4096 + 196608 + 1296;

                    if (deviceMemory > needed) {
                        //LOG_INFO() << "Memory check ok";
                        loadAndCompileKernel(nDev[di], pl);
                    } else {
                        //LOG_INFO() << "Memory check failed";
                        //LOG_INFO() << "Device reported " << deviceMemory / (1024*1024) << "MByte memory, " << needed/(1024*1024) << " are required ";
                    }
                } else {
                    // LOG_INFO() << "Device will not be used, it was not included in --devices parameter.";
                }

                curDiv++;
            }
        }
    }


    // Setup function called from outside
    void clHost::setup(minerBridge* br, vector<int32_t> devSel,  bool allowCPU) {
        bridge = br;
        detectPlatFormDevices(devSel, allowCPU);
    }


    // Function that will catch new work from the stratum interface and then queue the work on the device
    void clHost::queueKernels(uint32_t gpuIndex, clCallbackData* workData) {
        int64_t id;
        uint32_t difficulty;
        cl_ulong4 work;
        cl_ulong nonce;

        // Get a new set of work from the stratum interface
        bridge->getWork(&id,(uint64_t *) &nonce, (uint8_t *) &work, &difficulty);

        workData->workId = id;
        workData->nonce = (uint64_t) nonce;
        workData->difficulty = difficulty;


        // Kernel arguments for cleanCounter
        kernels[gpuIndex][0].setArg(0, buffers[gpuIndex][5]);
        kernels[gpuIndex][0].setArg(1, buffers[gpuIndex][6]);

        // Kernel arguments for round0
        kernels[gpuIndex][1].setArg(0, buffers[gpuIndex][0]);
        kernels[gpuIndex][1].setArg(1, buffers[gpuIndex][2]);
        kernels[gpuIndex][1].setArg(2, buffers[gpuIndex][5]);
        kernels[gpuIndex][1].setArg(3, work);
        kernels[gpuIndex][1].setArg(4, nonce);

        // Kernel arguments for round1
        kernels[gpuIndex][2].setArg(0, buffers[gpuIndex][0]);
        kernels[gpuIndex][2].setArg(1, buffers[gpuIndex][2]);
        kernels[gpuIndex][2].setArg(2, buffers[gpuIndex][1]);
        kernels[gpuIndex][2].setArg(3, buffers[gpuIndex][3]); 	// Index tree will be stored here
        kernels[gpuIndex][2].setArg(4, buffers[gpuIndex][5]);

        // Kernel arguments for round2
        kernels[gpuIndex][3].setArg(0, buffers[gpuIndex][1]);
        kernels[gpuIndex][3].setArg(1, buffers[gpuIndex][0]);	// Index tree will be stored here
        kernels[gpuIndex][3].setArg(2, buffers[gpuIndex][5]);

        // Kernel arguments for round3
        kernels[gpuIndex][4].setArg(0, buffers[gpuIndex][0]);
        kernels[gpuIndex][4].setArg(1, buffers[gpuIndex][1]); 	// Index tree will be stored here
        kernels[gpuIndex][4].setArg(2, buffers[gpuIndex][5]);

        // Kernel arguments for round4
        kernels[gpuIndex][5].setArg(0, buffers[gpuIndex][1]);
        kernels[gpuIndex][5].setArg(1, buffers[gpuIndex][2]); 	// Index tree will be stored here
        kernels[gpuIndex][5].setArg(2, buffers[gpuIndex][5]);

        // Kernel arguments for round5
        kernels[gpuIndex][6].setArg(0, buffers[gpuIndex][2]);
        kernels[gpuIndex][6].setArg(1, buffers[gpuIndex][4]); 	// Index tree will be stored here
        kernels[gpuIndex][6].setArg(2, buffers[gpuIndex][5]);

        // Kernel arguments for Combine
        kernels[gpuIndex][7].setArg(0, buffers[gpuIndex][0]);
        kernels[gpuIndex][7].setArg(1, buffers[gpuIndex][1]);
        kernels[gpuIndex][7].setArg(2, buffers[gpuIndex][2]);
        kernels[gpuIndex][7].setArg(3, buffers[gpuIndex][3]);
        kernels[gpuIndex][7].setArg(4, buffers[gpuIndex][4]);
        kernels[gpuIndex][7].setArg(5, buffers[gpuIndex][5]);
        kernels[gpuIndex][7].setArg(6, buffers[gpuIndex][6]);

        // Queue the kernels
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][0], cl::NDRange(0), cl::NDRange(12288), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][1], cl::NDRange(0), cl::NDRange(22369536), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][2], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].flush();
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][3], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][4], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][5], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][6], cl::NDRange(0), cl::NDRange(16777216), cl::NDRange(256), NULL, NULL);
        queues[gpuIndex].enqueueNDRangeKernel(kernels[gpuIndex][7], cl::NDRange(0), cl::NDRange(4096), cl::NDRange(16), NULL, NULL);
    }


    // this function will sumit the solutions done on GPU, then fetch new work and restart mining
    void clHost::callbackFunc(cl_int err , void* data){
        clCallbackData* workInfo = (clCallbackData*) data;
        uint32_t gpu = workInfo->gpuIndex;

        // Read the number of solutions of the last iteration
        uint32_t solutions = results[gpu][0];
        for (uint32_t  i=0; i<solutions; i++) {
            vector<uint32_t> indexes;
            indexes.assign(32,0);
            memcpy(indexes.data(), &results[gpu][4 + 32*i], sizeof(uint32_t) * 32);

            bridge->handleSolution(workInfo->workId,workInfo->nonce,indexes, workInfo->difficulty);
        }

        solutionCnt[gpu] += solutions;

        // Get new work and resume working
        if (bridge->hasWork() && restart) {
            queues[gpu].enqueueUnmapMemObject(buffers[gpu][6], results[gpu], NULL, NULL);
            queueKernels(gpu, &currentWork[gpu]);
            results[gpu] = (unsigned *) queues[gpu].enqueueMapBuffer(buffers[gpu][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[gpu], NULL);
            events[gpu].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[gpu]);
            queues[gpu].flush();
        } else {
            paused[gpu] = true;
            //LOG_INFO() << "Device will be paused, waiting for new work";
        }
    }

    void clHost::stopMining()
    {
        restart = false;
    }

    void clHost::startMining()
    {
        restart = true;
        // Start mining initially
        for (uint32_t i=0; i<devices.size() && restart; i++) {
            paused[i] = false;

            currentWork[i].gpuIndex = i;
            currentWork[i].host = (void*) this;
            queueKernels(i, &currentWork[i]);

            results[i] = (unsigned *) queues[i].enqueueMapBuffer(buffers[i][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[i], NULL);
            events[i].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[i]);
            queues[i].flush();
        }

        // While the mining is running print some statistics
        while (restart) {
            this_thread::sleep_for(std::chrono::seconds(15));

            // Print performance stats (roughly)
            //std::stringstream ss;

            //for (uint32_t i = 0; i < devices.size(); i++) {
            //    uint32_t sol = solutionCnt[i];
            //    solutionCnt[i] = 0;
            //    ss << fixed << std::setprecision(2) << (double)sol / 15.0 << " sol/s ";
            //}
            //LOG_INFO() << "Performance: " << ss.str();

            // Check if there are paused devices and restart them
            for (uint32_t i=0; i<devices.size(); i++) {
                if (paused[i] && bridge->hasWork() && restart) {
                    paused[i] = false;

                    // Same as above
                    queueKernels(i, &currentWork[i]);

                    results[i] = (unsigned *) queues[i].enqueueMapBuffer(buffers[i][6], CL_FALSE, CL_MAP_READ, 0, sizeof(cl_uint4) * 81, NULL, &events[i], NULL);
                    events[i].setCallback(CL_COMPLETE, &CCallbackFunc, (void*) &currentWork[i]);
                    queues[i].flush();
                }

            }
        }

        cl::Event::waitForEvents(events);
        //LOG_DEBUG() << "Miner stopped";
    }

    clHost::~clHost()
    {
        //LOG_INFO() << "clHost::~clHost()";
    }
}

namespace beam {

    class OpenCLMiner : public IExternalPOW
    {
    public:
        OpenCLMiner(const vector<int32_t>& devices)
            : _changed(false)
            , _stop(false)
            , _workProvider(BIND_THIS_MEMFN(on_solution))
            , _devices(devices)
        {
            _thread.start(BIND_THIS_MEMFN(thread_func));
            _minerThread.start(BIND_THIS_MEMFN(run_miner));
        }

        ~OpenCLMiner() override 
        {
            stop();
            _thread.join();
            _minerThread.join();
        }

    private:
        void new_job(
            const string& jobID,
            const Merkle::Hash& input,
            const Block::PoW& pow,
            const Height& height,
            const BlockFound& callback,
            const CancelCallback& cancelCallback
        ) override
        {
            {
                lock_guard<mutex> lk(_mutex);
                if (_currentJob.input == input)
                {
                    return;
                }
                _currentJob.jobID = jobID;
                _currentJob.input = input;
                _currentJob.pow = pow;
                _currentJob.callback = callback;
                _changed = true;
                _workProvider.feedJob(_currentJob);
            }
            _cond.notify_one();
        }

        void get_last_found_block(string& jobID, Block::PoW& pow) override
        {
            lock_guard<mutex> lk(_mutex);
            jobID = _lastFoundBlockID;
            pow = _lastFoundBlock;
        }

        void stop() override 
        {
            {
                lock_guard<mutex> lk(_mutex);
                _stop = true;
                _changed = true;
            }
            _ClHost.stopMining();
            _cond.notify_one();
        }

        void stop_current() override
        {
            // TODO do we need it?
        }

        bool TestDifficulty(const uint8_t* pSol, uint32_t nSol, Difficulty d) const
        {
            ECC::Hash::Value hv;
            ECC::Hash::Processor() << Blob(pSol, nSol) >> hv;

            return d.IsTargetReached(hv);
        }

        void thread_func()
        {
            while (true)
            {
                vector<Job> jobs;
                beam::IExternalPOW::BlockFound callback;
                {
                    unique_lock<mutex> lock(_mutex);

                    _cond.wait(lock, [this] { return !_solvedJobs.empty() || _stop; });
                    if (_stop)
                    {
                        return;
                    }
                    swap(jobs, _solvedJobs);
                    callback = _currentJob.callback;

                }
                for (const auto& job : jobs)
                {
                    if (!TestDifficulty(&job.pow.m_Indices[0], (uint32_t)job.pow.m_Indices.size(), job.pow.m_Difficulty))
                    {
                        continue;
                    }
                    {
                        unique_lock<mutex> lock(_mutex);
                        _lastFoundBlock = job.pow;
                        _lastFoundBlockID = job.jobID;
                    }
                    callback();
                }
            }
        }

        void run_miner()
        {
            bool cpuMine = false;

            _ClHost.setup(&_workProvider, _devices, cpuMine);

            while (!_workProvider.hasWork())
            {
                this_thread::sleep_for(chrono::milliseconds(200));
            }

            _ClHost.startMining();
        }

        void on_solution(Job&& job)
        {
            {
                unique_lock<mutex> lock(_mutex);
                _solvedJobs.push_back(move(job));
            }
            _cond.notify_one();
        }

    private:

        Job _currentJob;
        string _lastFoundBlockID;
        Block::PoW _lastFoundBlock;
        atomic<bool> _changed;
        bool _stop;
        Thread _thread;
        Thread _minerThread;
        mutex _mutex;
        condition_variable _cond;
        WorkProvider _workProvider;
        clHost _ClHost;
        vector<Job> _solvedJobs;
        const vector<int32_t> _devices;
        
    };

    IExternalPOW* create_opencl_solver(const vector<int32_t>& devices)
    {
        return new OpenCLMiner(devices);
    }

    class Logger {
        static Logger* g_logger;
    };
    Logger* Logger::g_logger = 0;

} //namespace

BOOST_DLL_ALIAS(beam::create_opencl_solver, create_opencl_solver)

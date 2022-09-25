#include <CL/cl.h>
#include <libclew/ocl_init.h>
#include <libutils/fast_random.h>
#include <libutils/timer.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>


template<typename T>
std::string to_string(T value) {
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

void reportError(cl_int err, const std::string &filename, int line, bool throwError = true) {
    if (CL_SUCCESS == err)
        return;

    // Таблица с кодами ошибок:
    // libs/clew/CL/cl.h:103
    // P.S. Быстрый переход к файлу в CLion: Ctrl+Shift+N -> cl.h (или даже с номером строки: cl.h:103) -> Enter
    std::string message = "OpenCL error code " + to_string(err) + " encountered at " + filename + ":" + to_string(line);
    if (throwError) {
        throw std::runtime_error(message);
    } else {
        std::cout << message;
    }
}

#define OCL_SAFE_CALL(expr) reportError(expr, __FILE__, __LINE__)

// OCL_SAFE_CALL throws exceptions, plain `clReleaseContext` call in the end of `main` won't work in such case
struct context_raii {
    cl_context value;

    ~context_raii() {
        reportError(clReleaseContext(value), __FILE__, __LINE__, false);
    }
};

struct command_queue_raii {
    cl_command_queue value;

    ~command_queue_raii() {
        reportError(clReleaseCommandQueue(value), __FILE__, __LINE__, false);
    }
};

struct mem_raii {
    cl_mem value;

    ~mem_raii() {
        reportError(clReleaseMemObject(value), __FILE__, __LINE__, false);
    }
};

struct program_raii {
    cl_program value;

    ~program_raii() {
        reportError(clReleaseProgram(value), __FILE__, __LINE__, false);
    }
};

struct kernel_raii {
    cl_kernel value;

    ~kernel_raii() {
        reportError(clReleaseKernel(value), __FILE__, __LINE__, false);
    }
};

cl_device_id chooseDevice() {
    cl_device_id cpuDevice = nullptr;

    cl_uint platformsCount = 0;
    OCL_SAFE_CALL(clGetPlatformIDs(0, nullptr, &platformsCount));
    std::vector<cl_platform_id> platforms(platformsCount);
    OCL_SAFE_CALL(clGetPlatformIDs(platformsCount, platforms.data(), nullptr));

    for (cl_platform_id platform : platforms) {
        cl_uint devicesCount = 0;
        OCL_SAFE_CALL(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &devicesCount));
        std::vector<cl_device_id> devices(devicesCount);
        OCL_SAFE_CALL(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, devicesCount, devices.data(), nullptr));

        for (cl_device_id device : devices) {
            cl_device_type deviceType = 0;
            OCL_SAFE_CALL(clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(cl_device_type), &deviceType, nullptr));
            switch (deviceType) {
                case CL_DEVICE_TYPE_CPU:
                    cpuDevice = device;
                    break;
                case CL_DEVICE_TYPE_GPU:
                    return device;
                default:
                    break;
            }
        }
    }

    if (cpuDevice == nullptr) {
        throw std::runtime_error("Unable to find neither a GPU nor a CPU device");
    }
    return cpuDevice;
}

int main() {
    // Пытаемся слинковаться с символами OpenCL API в runtime (через библиотеку clew)
    if (!ocl_init())
        throw std::runtime_error("Can't init OpenCL driver!");

    // TODO 1 По аналогии с предыдущим заданием узнайте, какие есть устройства, и выберите из них какое-нибудь
    // (если в списке устройств есть хоть одна видеокарта - выберите ее, если нету - выбирайте процессор)
    cl_device_id device = chooseDevice();

    // TODO 2 Создайте контекст с выбранным устройством
    // См. документацию https://www.khronos.org/registry/OpenCL/sdk/1.2/docs/man/xhtml/ -> OpenCL Runtime -> Contexts -> clCreateContext
    // Не забывайте проверять все возвращаемые коды на успешность (обратите внимание, что в данном случае метод возвращает
    // код по переданному аргументом errcode_ret указателю)
    // И хорошо бы сразу добавить в конце clReleaseContext (да, не очень RAII, но это лишь пример)
    cl_int errCode = CL_SUCCESS;
    context_raii context{ clCreateContext(nullptr, 1, &device, nullptr, nullptr, &errCode) };
    OCL_SAFE_CALL(errCode);

    // TODO 3 Создайте очередь выполняемых команд в рамках выбранного контекста и устройства
    // См. документацию https://www.khronos.org/registry/OpenCL/sdk/1.2/docs/man/xhtml/ -> OpenCL Runtime -> Runtime APIs -> Command Queues -> clCreateCommandQueue
    // Убедитесь, что в соответствии с документацией вы создали in-order очередь задач
    // И хорошо бы сразу добавить в конце clReleaseQueue (не забывайте освобождать ресурсы)
    command_queue_raii commandQueue{ clCreateCommandQueue(context.value, device, 0, &errCode) };
    OCL_SAFE_CALL(errCode);

    unsigned int n = 100 * 1000 * 1000;
    // Создаем два массива псевдослучайных данных для сложения и массив для будущего хранения результата
    std::vector<float> as(n, 0);
    std::vector<float> bs(n, 0);
    std::vector<float> cs(n, 0);
    FastRandom r(n);
    for (unsigned int i = 0; i < n; ++i) {
        as[i] = r.nextf();
        bs[i] = r.nextf();
    }
    std::cout << "Data generated for n=" << n << "!" << std::endl;

    // TODO 4 Создайте три буфера в памяти устройства (в случае видеокарты - в видеопамяти - VRAM) - для двух суммируемых массивов as и bs (они read-only) и для массива с результатом cs (он write-only)
    // См. Buffer Objects -> clCreateBuffer
    // Размер в байтах соответственно можно вычислить через sizeof(float)=4 и тот факт, что чисел в каждом массиве n штук
    // Данные в as и bs можно прогрузить этим же методом, скопировав данные из host_ptr=as.data() (и не забыв про битовый флаг, на это указывающий)
    // или же через метод Buffer Objects -> clEnqueueWriteBuffer
    // И хорошо бы сразу добавить в конце clReleaseMemObject (аналогично, все дальнейшие ресурсы вроде OpenCL под-программы, кернела и т.п. тоже нужно освобождать)
    size_t bufSize = as.size() * sizeof(float);
    mem_raii aBuf{ clCreateBuffer(
            context.value,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            bufSize,
            as.data(),
            &errCode)
    };
    OCL_SAFE_CALL(errCode);
    mem_raii bBuf{ clCreateBuffer(
            context.value,
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            bufSize,
            bs.data(),
            &errCode)
    };
    OCL_SAFE_CALL(errCode);
    mem_raii cBuf{ clCreateBuffer(
            context.value,
            CL_MEM_WRITE_ONLY | CL_MEM_USE_HOST_PTR,
            bufSize,
            cs.data(),
            &errCode)
    };
    OCL_SAFE_CALL(errCode);

    // TODO 6 Выполните TODO 5 (реализуйте кернел в src/cl/aplusb.cl)
    // затем убедитесь, что выходит загрузить его с диска (убедитесь что Working directory выставлена правильно - см. описание задания),
    // напечатав исходники в консоль (if проверяет, что удалось считать хоть что-то)
    std::string kernelSources;
    {
        std::ifstream file("src/cl/aplusb.cl");
        kernelSources = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (kernelSources.empty()) {
            throw std::runtime_error("Empty source file! May be you forgot to configure working directory properly?");
        }
        // std::cout << kernelSources << std::endl;
    }

    // TODO 7 Создайте OpenCL-подпрограмму с исходниками кернела
    // см. Runtime APIs -> Program Objects -> clCreateProgramWithSource
    // у string есть метод c_str(), но обратите внимание, что передать вам нужно указатель на указатель
    const char*kernelSourcesPtr = kernelSources.data();
    size_t kernelSourcesSize = kernelSources.size();
    program_raii program{clCreateProgramWithSource(context.value, 1, &kernelSourcesPtr, &kernelSourcesSize, &errCode)};
    OCL_SAFE_CALL(errCode);

    // TODO 8 Теперь скомпилируйте программу и напечатайте в консоль лог компиляции
    // см. clBuildProgram
    errCode = clBuildProgram(program.value, 1, &device, "", nullptr, nullptr);

    // А также напечатайте лог компиляции (он будет очень полезен, если в кернеле есть синтаксические ошибки - т.е. когда clBuildProgram вернет CL_BUILD_PROGRAM_FAILURE)
    // Обратите внимание, что при компиляции на процессоре через Intel OpenCL драйвер - в логе указывается, какой ширины векторизацию получилось выполнить для кернела
    // см. clGetProgramBuildInfo
    //    size_t log_size = 0;
    //    std::vector<char> log(log_size, 0);
    //    if (log_size > 1) {
    //        std::cout << "Log:" << std::endl;
    //        std::cout << log.data() << std::endl;
    //    }
    if (errCode != CL_SUCCESS) {
        size_t programBuildLogSize = 0;
        OCL_SAFE_CALL(clGetProgramBuildInfo(program.value, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &programBuildLogSize));
        std::vector<unsigned char> programBuildLog(programBuildLogSize + 1, 0);
        OCL_SAFE_CALL(clGetProgramBuildInfo(program.value, device, CL_PROGRAM_BUILD_LOG, programBuildLogSize, programBuildLog.data(), nullptr));
        if (programBuildLogSize > 1) {
            std::cout << "Program build log:" << std::endl;
            std::cout << programBuildLog.data() << std::endl;
        } else {
            std::cout << "Program build failed with no logs" << std::endl;
        }
    }
    OCL_SAFE_CALL(errCode);

    // TODO 9 Создайте OpenCL-kernel в созданной подпрограмме (в одной подпрограмме может быть несколько кернелов, но в данном случае кернел один)
    // см. подходящую функцию в Runtime APIs -> Program Objects -> Kernel Objects
    kernel_raii kernel = { clCreateKernel(program.value, "aplusb", &errCode) };
    OCL_SAFE_CALL(errCode);

    // TODO 10 Выставите все аргументы в кернеле через clSetKernelArg (as_gpu, bs_gpu, cs_gpu и число значений, убедитесь, что тип количества элементов такой же в кернеле)
    {
        cl_uint i = 0;
        OCL_SAFE_CALL(clSetKernelArg(kernel.value, i++, sizeof(aBuf.value), &aBuf.value)); // NOLINT(bugprone-sizeof-expression)
        OCL_SAFE_CALL(clSetKernelArg(kernel.value, i++, sizeof(bBuf.value), &bBuf.value)); // NOLINT(bugprone-sizeof-expression)
        OCL_SAFE_CALL(clSetKernelArg(kernel.value, i++, sizeof(cBuf.value), &cBuf.value)); // NOLINT(bugprone-sizeof-expression)
        OCL_SAFE_CALL(clSetKernelArg(kernel.value, i, sizeof(n), &n));
    }

    // TODO 11 Выше увеличьте n с 1000*1000 до 100*1000*1000 (чтобы дальнейшие замеры были ближе к реальности)

    // TODO 12 Запустите выполнения кернела:
    // - С одномерной рабочей группой размера 128
    // - В одномерном рабочем пространстве размера roundedUpN, где roundedUpN - наименьшее число, кратное 128 и при этом не меньшее n
    // - см. clEnqueueNDRangeKernel
    // - Обратите внимание, что, чтобы дождаться окончания вычислений (чтобы знать, когда можно смотреть результаты в cs_gpu) нужно:
    //   - Сохранить событие "кернел запущен" (см. аргумент "cl_event *event")
    //   - Дождаться завершения полунного события - см. в документации подходящий метод среди Event Objects
    {
        size_t workGroupSize = 128;
        size_t globalWorkSize = (n + workGroupSize - 1) / workGroupSize * workGroupSize;
        timer t;// Это вспомогательный секундомер, он замеряет время своего создания и позволяет усреднять время нескольких замеров
        for (unsigned int i = 0; i < 20; ++i) {
            // clEnqueueNDRangeKernel...
            cl_event event;
            OCL_SAFE_CALL(clEnqueueNDRangeKernel(
                commandQueue.value,
                kernel.value,
                1,
                nullptr,
                &globalWorkSize,
                &workGroupSize,
                0,
                nullptr,
                &event
            ));
            // clWaitForEvents...
            OCL_SAFE_CALL(clWaitForEvents(1, &event));
            t.nextLap();// При вызове nextLap секундомер запоминает текущий замер (текущий круг) и начинает замерять время следующего круга
        }
        // Среднее время круга (вычисления кернела) на самом деле считается не по всем замерам, а лишь с 20%-перцентайля по 80%-перцентайль (как и стандартное отклонение)
        // подробнее об этом - см. timer.lapsFiltered
        // P.S. чтобы в CLion быстро перейти к символу (функции/классу/много чему еще), достаточно нажать Ctrl+Shift+Alt+N -> lapsFiltered -> Enter
        std::cout << "Kernel average time: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;

        // TODO 13 Рассчитайте достигнутые гигафлопcы:
        // - Всего элементов в массивах по n штук
        // - Всего выполняется операций: операция a+b выполняется n раз
        // - Флопс - это число операций с плавающей точкой в секунду
        // - В гигафлопсе 10^9 флопсов
        // - Среднее время выполнения кернела равно t.lapAvg() секунд
        std::cout << "GFlops: " << (n / t.lapAvg() / 10e9) << std::endl;

        // TODO 14 Рассчитайте используемую пропускную способность обращений к видеопамяти (в гигабайтах в секунду)
        // - Всего элементов в массивах по n штук
        // - Размер каждого элемента sizeof(float)=4 байта
        // - Обращений к видеопамяти 2*n*sizeof(float) байт на чтение и 1*n*sizeof(float) байт на запись, т.е. итого 3*n*sizeof(float) байт
        // - В гигабайте 1024*1024*1024 байт
        // - Среднее время выполнения кернела равно t.lapAvg() секунд
        std::cout << "VRAM bandwidth: " << (3.0 * bufSize / t.lapAvg() / (1 << 30)) << " GB/s" << std::endl; // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    // TODO 15 Скачайте результаты вычислений из видеопамяти (VRAM) в оперативную память (RAM) - из cs_gpu в cs (и рассчитайте скорость трансфера данных в гигабайтах в секунду)
    {
        timer t;
        for (unsigned int i = 0; i < 20; ++i) {
            // clEnqueueReadBuffer...
            cl_event event;
            OCL_SAFE_CALL(clEnqueueReadBuffer(
                commandQueue.value,
                cBuf.value,
                CL_TRUE,
                0,
                bufSize,
                cs.data(),
                0,
                nullptr,
                &event
            ));
            OCL_SAFE_CALL(clWaitForEvents(1, &event));
            t.nextLap();
        }
        std::cout << "Result data transfer time: " << t.lapAvg() << "+-" << t.lapStd() << " s" << std::endl;
        std::cout << "VRAM -> RAM bandwidth: " << (bufSize / t.lapAvg() / (1 << 30)) << " GB/s" << std::endl; // NOLINT(cppcoreguidelines-narrowing-conversions)
    }

    // TODO 16 Сверьте результаты вычислений со сложением чисел на процессоре (и убедитесь, что если в кернеле сделать намеренную ошибку, то эта проверка поймает ошибку)
    for (unsigned int i = 0; i < n; ++i) {
        if (cs[i] != as[i] + bs[i]) {
            throw std::runtime_error("CPU and GPU results differ!");
        }
    }

    return 0;
}
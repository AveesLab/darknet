#include <stdlib.h>
#include "darknet.h"
#include "network.h"
#include "parser.h"
#include "detector.h"
#include "option_list.h"

#include <pthread.h>
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>

#ifdef NVTX
#include "nvToolsExt.h"
#endif

#ifdef MEASURE
#ifdef WIN32
#include <time.h>
#include "gettimeofday.h"
#else
#include <sys/time.h>
#endif
#endif
pthread_barrier_t barrier;

// static int coreIDOrder[MAXCORES] = {3, 6, 9, 1, 4, 7, 10, 2, 5, 8, 11};
static int coreIDOrder[MAXCORES] = {0,1,2,3,4,5,6,7,8,9,10,11};
static network net_list[MAXCORES];
static pthread_mutex_t mutex_init = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t mutex_gpu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int current_thread = 1;

typedef struct thread_data_t{
    char *datacfg;
    char *cfgfile;
    char *weightfile;
    char *filename;
    float thresh;
    float hier_thresh;
    int dont_show;
    int ext_output;
    int save_labels;
    char *outfile;
    int letter_box;
    int benchmark_layers;
    int thread_id;
    int num_thread;
    bool isTest;
    bool isSet;
} thread_data_t;

#ifdef MEASURE
static double core_id_list[1000];
static double start_preprocess[1000];
static double end_preprocess[1000];
static double e_preprocess[1000];
static double e_preprocess_max[1000];

static double start_infer[1000];
static double start_gpu_waiting[1000];
static double start_gpu_infer[1000];
static double end_gpu_infer[1000];
static double start_cpu_infer[1000];
static double end_cpu_infer[1000];
static double end_infer[1000];

static double waiting_gpu[1000];
static double e_gpu_infer[1000];
static double e_gpu_infer_max[1000];
static double e_cpu_infer[1000];
static double e_infer[1000];

static double start_postprocess[1000];
static double end_postprocess[1000];
static double e_postprocess[1000];

static int optimal_core;
#endif

static double execution_time[1000];
static double execution_time_max[1000];

static float avg_preprocess_time;
static float avg_gpu_infer_time;
static float avg_execution_time;

static float max_preprocess_time;
static float max_gpu_infer_time;
static float max_execution_time;
static float sleep_time;
static float R;


static double average(double arr[]){
    double sum;
    int i;
    for(i = 3; i < num_exp; i++) {
        sum += arr[i];
    }
    return sum / (num_exp-3);
}

#ifdef MEASURE
static int compare(const void *a, const void *b) {
    double valueA = *((double *)a + 1);
    double valueB = *((double *)b + 1);

    if (valueA < valueB) return -1;
    if (valueA > valueB) return 1;
    return 0;
}

static int write_result(char *file_path) 
{
    static int exist=0;
    FILE *fp;
    int tick = 0;

    fp = fopen(file_path, "w+");

    int i;
    if (fp == NULL) 
    {
        /* make directory */
        while(!exist)
        {
            int result;

            usleep(10 * 1000);

            result = mkdir(MEASUREMENT_PATH, 0766);
            if(result == 0) { 
                exist = 1;

                fp = fopen(file_path,"w+");
            }

            if(tick == 100)
            {
                fprintf(stderr, "\nERROR: Fail to Create %s\n", file_path);

                return -1;
            }
            else tick++;
        }
    }
    else printf("\nWrite output in %s\n", file_path); 

    double sum_measure_data[num_exp * optimal_core][21];
    for(i = 0; i < num_exp * optimal_core; i++)
    {
        sum_measure_data[i][0] = core_id_list[i];
        sum_measure_data[i][1] = start_preprocess[i];
        sum_measure_data[i][2] = e_preprocess[i];
        sum_measure_data[i][3] = end_preprocess[i];
        sum_measure_data[i][4] = start_infer[i]; 
        sum_measure_data[i][5] = start_cpu_infer[i];
        sum_measure_data[i][6] = e_cpu_infer[i];
        sum_measure_data[i][7] = end_cpu_infer[i];
        sum_measure_data[i][8] = start_gpu_waiting[i];
        sum_measure_data[i][9] = waiting_gpu[i];
        sum_measure_data[i][10] = start_gpu_infer[i];
        sum_measure_data[i][11] = e_gpu_infer[i];
        sum_measure_data[i][12] = end_gpu_infer[i];
        sum_measure_data[i][13] = end_infer[i];
        sum_measure_data[i][14] = e_infer[i];
        sum_measure_data[i][15] = start_postprocess[i];
        sum_measure_data[i][16] = e_postprocess[i];
        sum_measure_data[i][17] = end_postprocess[i];
        sum_measure_data[i][18] = execution_time[i];
        sum_measure_data[i][19] = 0.0;
        sum_measure_data[i][20] = 0.0;
    }

    qsort(sum_measure_data, sizeof(sum_measure_data)/sizeof(sum_measure_data[0]), sizeof(sum_measure_data[0]), compare);

    int startIdx = optimal_core * 10; // Delete some ROWs
    double new_sum_measure_data[sizeof(sum_measure_data)/sizeof(sum_measure_data[0])-startIdx][sizeof(sum_measure_data[0])];

    int newIndex = 0;
    for (int i = startIdx; i < sizeof(sum_measure_data)/sizeof(sum_measure_data[0]); i++) {
        for (int j = 0; j < sizeof(sum_measure_data[0]); j++) {
            new_sum_measure_data[newIndex][j] = sum_measure_data[i][j];
        }
        newIndex++;
    }

    fprintf(fp, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
            "core_id", 
            "start_preprocess", "e_preprocess", "end_preprocess", 
            "start_infer", 
            "start_cpu_infer", "e_cpu_infer", "end_cpu_infer", 
            "start_gpu_waiting", "waiting_gpu", 
            "start_gpu_infer", "e_gpu_infer", "end_gpu_infer", "end_infer",
            "e_infer",
            "start_postprocess", "e_postprocess", "end_postprocess", 
            "execution_time", "frame_rate",
            "optimal_core");

    double frame_rate = 1000 / ( (new_sum_measure_data[(sizeof(new_sum_measure_data)/sizeof(new_sum_measure_data[0]))-1][16]-new_sum_measure_data[0][1]) / (sizeof(new_sum_measure_data)/sizeof(new_sum_measure_data[0])) );

    for(i = 0; i < num_exp * optimal_core - startIdx; i++)
    {
        new_sum_measure_data[i][19] = frame_rate;
        new_sum_measure_data[i][20] = (double)optimal_core;

        fprintf(fp, "%0.0f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.0f\n",  
                new_sum_measure_data[i][0], new_sum_measure_data[i][1], new_sum_measure_data[i][2], new_sum_measure_data[i][3], 
                new_sum_measure_data[i][4], new_sum_measure_data[i][5], new_sum_measure_data[i][6], new_sum_measure_data[i][7], 
                new_sum_measure_data[i][8], new_sum_measure_data[i][9], new_sum_measure_data[i][10], new_sum_measure_data[i][11], 
                new_sum_measure_data[i][12], new_sum_measure_data[i][13], new_sum_measure_data[i][14], new_sum_measure_data[i][15],
                new_sum_measure_data[i][16], new_sum_measure_data[i][17], new_sum_measure_data[i][18], new_sum_measure_data[i][19], new_sum_measure_data[i][20]);
    }
    
    fclose(fp);

    return 1;
}
#endif

#ifdef GPU
static void threadFunc(thread_data_t data)
{
    // __CPU AFFINITY SETTING__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreIDOrder[data.thread_id], &cpuset); // cpu core index

    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (ret != 0) {
        fprintf(stderr, "pthread_setaffinity_np() failed \n");
        exit(0);
    } 

    // __GPU SETUP__
#ifdef GPU   // GPU
    if(gpu_index >= 0){
        cuda_set_device(gpu_index);
        CHECK_CUDA(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync));
    }
#ifdef CUDNN_HALF
    printf(" CUDNN_HALF=1 \n");
#endif  // CUDNN_HALF
#else
    gpu_index = -1;
    printf(" GPU isn't used \n");
    init_cpu();
#endif  // GPU

    list *options = read_data_cfg(data.datacfg);
    char *name_list = option_find_str(options, "names", "data/names.list");
    int names_size = 0;
    char **names = get_labels_custom(name_list, &names_size); //get_labels(name_list)

    char buff[256];
    char *input = buff;

    image **alphabet = load_alphabet();

    float nms = .45;    // 0.4F
    double time;

    int top = 5;
    int index, i, j, k = 0;
    int* indexes = (int*)xcalloc(top, sizeof(int));

    int nboxes;
    detection *dets;

    image im, resized, cropped;
    float *X, *predictions;

    char *target_model = "yolo";
    int object_detection = strstr(data.cfgfile, target_model);

    int device = 1; // Choose CPU or GPU
    static int skip_layers[1000];
    static gpu_yolo;

    pthread_mutex_lock(&mutex_init);
    // double start_1 = get_time_in_ms();
    if (!data.isSet) {
        network net_init = parse_network_cfg_custom(data.cfgfile, 1, 1, device); // set batch=1

        if (data.weightfile) {
            load_weights(&net_init, data.weightfile);
        }
        if (net_init.letter_box) data.letter_box = 1;
        net_init.benchmark_layers = data.benchmark_layers;
        fuse_conv_batchnorm(net_init);
        calculate_binary_weights(net_init);

        net_list[data.thread_id] = net_init;
    }
    network net = net_list[data.thread_id];
    // network net = parse_network_cfg_custom(data.cfgfile, 1, 1, device);
    // printf("parse_network_cfg_custom : %.3lf ms\n", get_time_in_ms() - start_1);
    pthread_mutex_unlock(&mutex_init);

    layer l = net.layers[net.n - 1];


    srand(2222222);

    if (data.filename) strncpy(input, data.filename, 256);
    else printf("Error! File is not exist.");
    double remaining_time = 0.0;
    double wait_start = 0.0;
    double wait_end = 0.0;
    double work_time = 0.0;

    double start_task_time = 0.0;
    for (i = 0; i < num_exp; i++) {

        if (!data.isTest) {
            if (i == 5) {
                pthread_barrier_wait(&barrier);
                //usleep(R * (data.thread_id - 1) * 1000);

                // Busy wait for the remaining time
                remaining_time = R * (data.thread_id - 1);
                wait_start, wait_end, work_time = 0.0, 0.0, 0.0;
                
                if (remaining_time > 0) {
                    wait_start = get_time_in_ms();
                    wait_end;
                    do {
                        wait_end = get_time_in_ms();
                        work_time = wait_end - wait_start;
                    } while(work_time < remaining_time);
                }

            }
        }

#ifdef MEASURE
        int count = i * data.num_thread + data.thread_id - 1;
#endif

#ifdef NVTX
        char task[100];
        sprintf(task, "Task (cpu: %d)", data.thread_id);
        nvtxRangeId_t nvtx_task;
        nvtx_task = nvtxRangeStartA(task);
#endif

#ifdef MEASURE
        // printf("\nThread %d is set to CPU core %d count(%d) : %d \n\n", data.thread_id, sched_getcpu(), data.thread_id, count);
#else
        printf("\nThread %d is set to CPU core %d\n\n", data.thread_id, sched_getcpu());
#endif

        time = get_time_in_ms();
        // __Preprocess__
#ifdef MEASURE
        start_preprocess[count] = get_time_in_ms();
#endif
        im = load_image(input, 0, 0, net.c);
        resized = resize_min(im, net.w);
        cropped = crop_image(resized, (resized.w - net.w)/2, (resized.h - net.h)/2, net.w, net.h);
        X = cropped.data;

#ifdef MEASURE
        end_preprocess[count] = get_time_in_ms();
        e_preprocess[count] = end_preprocess[count] - start_preprocess[count];
#endif

        // __Inference__
        // if (device) predictions = network_predict(net, X);
        // else predictions = network_predict_cpu(net, X);

#ifdef MEASURE
        start_infer[count] = get_time_in_ms();
#endif

        static int gpu_yolo;
        gpu_yolo = 0;

        network_state state = {0};
        state.index = 0;
        state.net = net;
        state.input = X;
        state.truth = 0;
        state.train = 0;
        state.delta = 0;

        // CPU Inference

#ifdef NVTX
        char task_cpu[100];
        sprintf(task_cpu, "Task (cpu: %d) - CPU Inference", data.thread_id);
        nvtxRangeId_t nvtx_task_cpu;
        nvtx_task_cpu = nvtxRangeStartA(task_cpu);
#endif

#ifdef MEASURE
        start_cpu_infer[count] = get_time_in_ms();
#endif
        state.workspace = net.workspace_cpu;
        for(j = 0; j < gLayer; ++j){
            state.index = j;
            l = net.layers[j];
            if(l.delta && state.train && l.train){
                scal_cpu(l.outputs * l.batch, 0, l.delta, 1);
            }
            l.forward(l, state);
            if (skip_layers[j]){
                cuda_push_array(l.output_gpu, l.output, l.outputs * l.batch);
            }
            state.input = l.output;
        }

#ifdef MEASURE
        end_cpu_infer[count] = get_time_in_ms();
        start_gpu_waiting[count] = get_time_in_ms();
#endif

        pthread_mutex_lock(&mutex_gpu);

        while(data.thread_id != current_thread) {
            pthread_cond_wait(&cond, &mutex_gpu);
        }
        
        if (net.gpu_index != cuda_get_device())
            cuda_set_device(net.gpu_index);
        cuda_push_array(l.output_gpu, l.output, l.outputs * l.batch);
        state.input = l.output_gpu;

        CHECK_CUDA(cudaStreamSynchronize(get_cuda_stream()));

#ifdef NVTX
        nvtxRangeEnd(nvtx_task_cpu);
#endif

        // GPU Inference
#ifdef NVTX
        char task_gpu[100];
        sprintf(task_gpu, "Task (cpu: %d) - GPU Inference", data.thread_id);
        nvtxRangeId_t nvtx_task_gpu;
        nvtx_task_gpu = nvtxRangeStartA(task_gpu);
#endif

#ifdef MEASURE
        start_gpu_infer[count] = get_time_in_ms();
#endif

        state.workspace = net.workspace;
        for(j = gLayer; j < net.n; ++j){
            state.index = j;
            l = net.layers[j];
            if(l.delta && state.train && l.train){
                fill_ongpu(l.outputs * l.batch, 0, l.delta_gpu, 1);
            }
            l.forward_gpu(l, state);
            state.input = l.output_gpu;
        }

        CHECK_CUDA(cudaStreamSynchronize(get_cuda_stream()));

        if (gLayer != net.n) predictions = get_network_output_gpu(net);
        else predictions = get_network_output(net, 0);
        reset_wait_stream_events();
        //cuda_free(state.input);   // will be freed in the free_network()


        if (data.thread_id == data.num_thread) {
            current_thread = 1;
        } else {
            current_thread++;
        }
        
        pthread_cond_broadcast(&cond);
        pthread_mutex_unlock(&mutex_gpu);
        end_gpu_infer[count] = get_time_in_ms();

#ifdef NVTX
        nvtxRangeEnd(nvtx_task_gpu);
#endif

#ifdef MEASURE
        end_infer[count] = get_time_in_ms();
        e_cpu_infer[count] = end_cpu_infer[count] - start_cpu_infer[count];
        waiting_gpu[count] = start_gpu_infer[count] - start_gpu_waiting[count];
        e_gpu_infer[count] = end_gpu_infer[count] - start_gpu_infer[count];
        e_infer[count] = end_infer[count] - start_infer[count];
#endif

        // __Postprecess__
#ifdef MEASURE
        start_postprocess[count] = get_time_in_ms();
#endif

        // __NMS & TOP acccuracy__
        if (object_detection) {
            dets = get_network_boxes(&net, im.w, im.h, data.thresh, data.hier_thresh, 0, 1, &nboxes, data.letter_box);
            if (nms) {
                if (l.nms_kind == DEFAULT_NMS) do_nms_sort(dets, nboxes, l.classes, nms);
                else diounms_sort(dets, nboxes, l.classes, nms, l.nms_kind, l.beta_nms);
            }
            draw_detections_v3(im, dets, nboxes, data.thresh, names, alphabet, l.classes, data.ext_output);
        }
        else {
            if(net.hierarchy) hierarchy_predictions(predictions, net.outputs, net.hierarchy, 0);
            top_k(predictions, net.outputs, top, indexes);
            for(j = 0; j < top; ++j){
                index = indexes[j];
                if(net.hierarchy) printf("%d, %s: %f, parent: %s \n",index, names[index], predictions[index], (net.hierarchy->parent[index] >= 0) ? names[net.hierarchy->parent[index]] : "Root");

#ifndef MEASURE
                else printf("%s: %f\n",names[index], predictions[index]);
#endif

            }
        }

        // __Display__
        // if (!data.dont_show) {
        //     show_image(im, "predictions");
        //     wait_key_cv(1);
        // }

#ifdef MEASURE
        end_postprocess[count] = get_time_in_ms();
        e_postprocess[count] = end_postprocess[count] - start_postprocess[count];
        execution_time[count] = end_postprocess[count] - start_preprocess[count];
        core_id_list[count] = (double)sched_getcpu();
        // printf("\n%s: Predicted in %0.3f milli-seconds.\n", input, e_infer[count]);
#else
        execution_time[i] = get_time_in_ms() - time;
        frame_rate[i] = 1000.0 / (execution_time[i] / data.num_thread); // N thread
        printf("\n%s: Predicted in %0.3f milli-seconds. (%0.3lf fps)\n", input, execution_time[i], frame_rate[i]);
#endif

        // Busy wait for the remaining time
        if (!data.isTest) {
            remaining_time = R * optimal_core - (get_time_in_ms() - start_preprocess[count]);
            wait_start, wait_end, work_time = 0.0, 0.0, 0.0;
            
            if (remaining_time > 0) {
                wait_start = get_time_in_ms();
                wait_end;
                do {
                    wait_end = get_time_in_ms();
                    work_time = wait_end - wait_start;
                } while(work_time < remaining_time);
            }
        }

        // free memory
        free_image(im);
        free_image(resized);
        free_image(cropped);

#ifdef NVTX
        nvtxRangeEnd(nvtx_task);
#endif
    }

    // free memory
    free_detections(dets, nboxes);
    free_ptrs((void**)names, net.layers[net.n - 1].classes);
    free_list_contents_kvp(options);
    free_list(options);
    free_alphabet(alphabet);
    // free_network(net); // Error occur
    pthread_exit(NULL);

}

void gpu_accel_reverse(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int theoretical_exp, int theo_thread, int ext_output, int save_labels, char *outfile, int letter_box, int benchmark_layers)
{

    pthread_t threads[MAXCORES - 1];
    int rc;
    int i;

    thread_data_t data[MAXCORES - 1];

#ifdef MEASURE
        optimal_core = 11;

        R = 0.0;
        sleep_time = 0.0;
        max_preprocess_time = 0.0;
        max_gpu_infer_time = 0.0;
        max_execution_time = 0.0;
        avg_preprocess_time = 0.0;
        avg_gpu_infer_time = 0.0;
        avg_execution_time = 0.0;

        printf("\n\nGPU-accelerated with Jitter Compensation (CS: \"GPU\")\n");
        printf("\n::TEST:: GPU-Accel-Reverse with %d threads with %d gpu-layer\n", optimal_core, gLayer);

        for (i = 0; i < optimal_core; i++) {
            data[i].datacfg = datacfg;
            data[i].cfgfile = cfgfile;
            data[i].weightfile = weightfile;
            data[i].filename = filename;
            data[i].thresh = thresh;
            data[i].hier_thresh = hier_thresh;
            data[i].dont_show = dont_show;
            data[i].ext_output = ext_output;
            data[i].save_labels = save_labels;
            data[i].outfile = outfile;
            data[i].letter_box = letter_box;
            data[i].benchmark_layers = benchmark_layers;
            data[i].thread_id = i + 1;
            data[i].num_thread = optimal_core;
            data[i].isTest = true;
            data[i].isSet = false;
            rc = pthread_create(&threads[i], NULL, threadFunc, &data[i]);
            if (rc) {
                printf("Error: Unable to create thread, %d\n", rc);
                exit(-1);
            }
        }

        for (i = 0; i < optimal_core; i++) {
            pthread_join(threads[i], NULL);
        }

        int startIdx = 5 * optimal_core;
        for (i = startIdx; i < optimal_core * num_exp; i++) {
            // max_preprocess_time = MAX(max_preprocess_time, e_preprocess[i]); // Pre
            max_gpu_infer_time = MAX(max_gpu_infer_time, e_gpu_infer[i]); // GPU_infer
            max_execution_time = MAX(max_execution_time, (e_preprocess[i]+e_gpu_infer[i]+e_cpu_infer[i]+e_postprocess[i])); // CPU_infer + Post

            // avg_preprocess_time += e_preprocess[i];
            avg_gpu_infer_time += e_gpu_infer[i];
            avg_execution_time += (execution_time[i]-waiting_gpu[i]);
        }

        // avg_preprocess_time = avg_preprocess_time / (optimal_core * num_exp - startIdx + 1);
        avg_gpu_infer_time = avg_gpu_infer_time / (optimal_core * num_exp - startIdx + 1);
        avg_execution_time = avg_execution_time / (optimal_core * num_exp - startIdx + 1);

        double wcet_ratio = 1.07;
        // max_preprocess_time = max_preprocess_time * wcet_ratio; // Pre
        max_gpu_infer_time = avg_gpu_infer_time * wcet_ratio; // GPU_infer
        max_execution_time = avg_execution_time * wcet_ratio; // CPU_infer + Post

        // max_execution_time = max_preprocess_time + max_gpu_infer_time + max_execution_time; // Pre + GPU_infer + CPU_infer + Post

        // printf("\navg preprocess time (max) : %0.2lf (%0.2lf) \n", avg_preprocess_time, max_preprocess_time);
        printf("WCET ratio : %lf \n", wcet_ratio);
        printf("avg gpu inference time (max) : %0.2lf (%0.2lf) \n", avg_gpu_infer_time, max_gpu_infer_time);
        printf("avg execution time (max) : %0.2lf (%0.2lf) \n", avg_execution_time, max_execution_time);

        R = MAX(max_gpu_infer_time, max_execution_time / optimal_core);
        sleep_time = R * optimal_core - max_execution_time;
        if (sleep_time < 0) sleep_time = 0.0;
        printf("R : %lf \n", R);
        printf("sleep_time (R * n) : %lf (%lf) \n", sleep_time , R * optimal_core);

        printf("\n\n::EXP-1:: GPU-Accel-Reverse with %d threads with %d gpu-layer\n", optimal_core, gLayer);

        pthread_barrier_init(&barrier, NULL, optimal_core);
        for (i = 0; i < optimal_core; i++) {
            data[i].datacfg = datacfg;
            data[i].cfgfile = cfgfile;
            data[i].weightfile = weightfile;
            data[i].filename = filename;
            data[i].thresh = thresh;
            data[i].hier_thresh = hier_thresh;
            data[i].dont_show = dont_show;
            data[i].ext_output = ext_output;
            data[i].save_labels = save_labels;
            data[i].outfile = outfile;
            data[i].letter_box = letter_box;
            data[i].benchmark_layers = benchmark_layers;
            data[i].thread_id = i + 1;
            data[i].num_thread = optimal_core;
            data[i].isTest = false;
            data[i].isSet = true;
            rc = pthread_create(&threads[i], NULL, threadFunc, &data[i]);
            if (rc) {
                printf("Error: Unable to create thread, %d\n", rc);
                exit(-1);
            }
        }

        for (i = 0; i < optimal_core; i++) {
            pthread_join(threads[i], NULL);
        }


        startIdx = 5 * optimal_core;
        for (i = startIdx; i < optimal_core * num_exp; i++) {
            // max_preprocess_time = MAX(max_preprocess_time, e_preprocess[i]); // Pre
            max_gpu_infer_time = MAX(max_gpu_infer_time, e_gpu_infer[i]); // GPU_infer
            max_execution_time = MAX(max_execution_time, (e_preprocess[i]+e_gpu_infer[i]+e_cpu_infer[i]+e_postprocess[i])); // CPU_infer + Post

            // avg_preprocess_time += e_preprocess[i];
            avg_gpu_infer_time += e_gpu_infer[i];
            avg_execution_time += (execution_time[i]-waiting_gpu[i]);
        }

        // avg_preprocess_time = avg_preprocess_time / (optimal_core * num_exp - startIdx + 1);
        avg_gpu_infer_time = avg_gpu_infer_time / (optimal_core * num_exp - startIdx + 1);
        avg_execution_time = avg_execution_time / (optimal_core * num_exp - startIdx + 1);

        wcet_ratio = 1.03;
        // max_preprocess_time = max_preprocess_time * wcet_ratio; // Pre
        max_gpu_infer_time = avg_gpu_infer_time * wcet_ratio; // GPU_infer
        max_execution_time = avg_execution_time * wcet_ratio; // CPU_infer + Post

        // max_execution_time = max_preprocess_time + max_gpu_infer_time + max_execution_time; // Pre + GPU_infer + CPU_infer + Post

        // printf("\navg preprocess time (max) : %0.2lf (%0.2lf) \n", avg_preprocess_time, max_preprocess_time);
        printf("WCET ratio : %lf \n", wcet_ratio);
        printf("avg gpu inference time (max) : %0.2lf (%0.2lf) \n", avg_gpu_infer_time, max_gpu_infer_time);
        printf("avg execution time (max) : %0.2lf (%0.2lf) \n", avg_execution_time, max_execution_time);

        R = MAX(max_gpu_infer_time, max_execution_time / optimal_core);
        sleep_time = R * optimal_core - max_execution_time;
        if (sleep_time < 0) sleep_time = 0.0;
        printf("R : %lf \n", R);
        printf("sleep_time (R * n) : %lf (%lf) \n", sleep_time , R * optimal_core);

        printf("\n\n::EXP-2:: GPU-Accel-Reverse with %d threads with %d gpu-layer\n", optimal_core, gLayer);

        pthread_barrier_init(&barrier, NULL, optimal_core);
        for (i = 0; i < optimal_core; i++) {
            data[i].datacfg = datacfg;
            data[i].cfgfile = cfgfile;
            data[i].weightfile = weightfile;
            data[i].filename = filename;
            data[i].thresh = thresh;
            data[i].hier_thresh = hier_thresh;
            data[i].dont_show = dont_show;
            data[i].ext_output = ext_output;
            data[i].save_labels = save_labels;
            data[i].outfile = outfile;
            data[i].letter_box = letter_box;
            data[i].benchmark_layers = benchmark_layers;
            data[i].thread_id = i + 1;
            data[i].num_thread = optimal_core;
            data[i].isTest = false;
            data[i].isSet = true;
            rc = pthread_create(&threads[i], NULL, threadFunc, &data[i]);
            if (rc) {
                printf("Error: Unable to create thread, %d\n", rc);
                exit(-1);
            }
        }

        for (i = 0; i < optimal_core; i++) {
            pthread_join(threads[i], NULL);
        }


        startIdx = 5 * optimal_core;
        for (i = startIdx; i < optimal_core * num_exp; i++) {
            // max_preprocess_time = MAX(max_preprocess_time, e_preprocess[i]); // Pre
            max_gpu_infer_time = MAX(max_gpu_infer_time, e_gpu_infer[i]); // GPU_infer
            max_execution_time = MAX(max_execution_time, (e_preprocess[i]+e_gpu_infer[i]+e_cpu_infer[i]+e_postprocess[i])); // CPU_infer + Post

            // avg_preprocess_time += e_preprocess[i];
            avg_gpu_infer_time += e_gpu_infer[i];
            avg_execution_time += (execution_time[i]-waiting_gpu[i]);
        }

        // avg_preprocess_time = avg_preprocess_time / (optimal_core * num_exp - startIdx + 1);
        avg_gpu_infer_time = avg_gpu_infer_time / (optimal_core * num_exp - startIdx + 1);
        avg_execution_time = avg_execution_time / (optimal_core * num_exp - startIdx + 1);

        wcet_ratio = 1.03;
        // max_preprocess_time = max_preprocess_time * wcet_ratio; // Pre
        max_gpu_infer_time = avg_gpu_infer_time * wcet_ratio; // GPU_infer
        max_execution_time = avg_execution_time * wcet_ratio; // CPU_infer + Post

        // max_execution_time = max_preprocess_time + max_gpu_infer_time + max_execution_time; // Pre + GPU_infer + CPU_infer + Post

        // printf("\navg preprocess time (max) : %0.2lf (%0.2lf) \n", avg_preprocess_time, max_preprocess_time);
        printf("WCET ratio : %lf \n", wcet_ratio);
        printf("avg gpu inference time (max) : %0.2lf (%0.2lf) \n", avg_gpu_infer_time, max_gpu_infer_time);
        printf("avg execution time (max) : %0.2lf (%0.2lf) \n", avg_execution_time, max_execution_time);

        R = MAX(max_gpu_infer_time, max_execution_time / optimal_core);
        optimal_core = (int)ceil(max_execution_time / R);
        sleep_time = R * optimal_core - max_execution_time;
        if (sleep_time < 0) sleep_time = 0.0;
        printf("R : %lf \n", R);
        printf("sleep_time (R * n) : %lf (%lf) \n", sleep_time , R * optimal_core);

        printf("\n\n::EXP-3:: GPU-Accel-Reverse with %d threads with %d gpu-layer\n", optimal_core, gLayer);

        pthread_barrier_init(&barrier, NULL, optimal_core);
        for (i = 0; i < optimal_core; i++) {
            data[i].datacfg = datacfg;
            data[i].cfgfile = cfgfile;
            data[i].weightfile = weightfile;
            data[i].filename = filename;
            data[i].thresh = thresh;
            data[i].hier_thresh = hier_thresh;
            data[i].dont_show = dont_show;
            data[i].ext_output = ext_output;
            data[i].save_labels = save_labels;
            data[i].outfile = outfile;
            data[i].letter_box = letter_box;
            data[i].benchmark_layers = benchmark_layers;
            data[i].thread_id = i + 1;
            data[i].num_thread = optimal_core;
            data[i].isTest = false;
            data[i].isSet = true;
            rc = pthread_create(&threads[i], NULL, threadFunc, &data[i]);
            if (rc) {
                printf("Error: Unable to create thread, %d\n", rc);
                exit(-1);
            }
        }

        for (i = 0; i < optimal_core; i++) {
            pthread_join(threads[i], NULL);
        }

#else
    printf("\n\nGPU-Accel-Reverse with %d threads with %d gpu-layer\n", num_thread, gLayer);
    for (i = 0; i < num_thread; i++) {
        data[i].datacfg = datacfg;
        data[i].cfgfile = cfgfile;
        data[i].weightfile = weightfile;
        data[i].filename = filename;
        data[i].thresh = thresh;
        data[i].hier_thresh = hier_thresh;
        data[i].dont_show = dont_show;
        data[i].ext_output = ext_output;
        data[i].save_labels = save_labels;
        data[i].outfile = outfile;
        data[i].letter_box = letter_box;
        data[i].benchmark_layers = benchmark_layers;
        data[i].thread_id = i + 1;
        rc = pthread_create(&threads[i], NULL, threadFunc, &data[i]);
        if (rc) {
            printf("Error: Unable to create thread, %d\n", rc);
            exit(-1);
        }
    }

    for (i = 0; i < num_thread; i++) {
        pthread_join(threads[i], NULL);
    }
#endif

#ifdef MEASURE
    char file_path[256] = "measure/";

    char* model_name = malloc(strlen(cfgfile) + 1);
    strncpy(model_name, cfgfile + 6, (strlen(cfgfile)-10));
    model_name[strlen(cfgfile)-10] = '\0';
    
    if (theoretical_exp) {
        if (theo_thread == 1) strcat(file_path, "gpu-accel-reverse_1thread/");
        else if (theo_thread == 2) strcat(file_path, "gpu-accel-reverse_2thread/");
        else if (theo_thread == 3) strcat(file_path, "gpu-accel-reverse_3thread/");
        else if (theo_thread == 4) strcat(file_path, "gpu-accel-reverse_4thread/");
        else if (theo_thread == 5) strcat(file_path, "gpu-accel-reverse_5thread/");
        else if (theo_thread == 6) strcat(file_path, "gpu-accel-reverse_6thread/");
        else if (theo_thread == 7) strcat(file_path, "gpu-accel-reverse_7thread/");
        else if (theo_thread == 8) strcat(file_path, "gpu-accel-reverse_8thread/");
        else if (theo_thread == 9) strcat(file_path, "gpu-accel-reverse_9thread/");
        else if (theo_thread == 10) strcat(file_path, "gpu-accel-reverse_10thread/");
        else if (theo_thread == 11) strcat(file_path, "gpu-accel-reverse_11thread/");
        else printf("\nError: Please set -theo_thread {thread_num}\n");
    }
    else strcat(file_path, "gpu-accel-reverse/");

    strcat(file_path, model_name);
    strcat(file_path, "/");

    strcat(file_path, "gpu-accel-reverse_");

    char gpu_portion[20];
    if (theoretical_exp && (theo_thread > 1)) sprintf(gpu_portion, "%03dglayer", gLayer);
    else sprintf(gpu_portion, "%03dglayer", gLayer);
    strcat(file_path, gpu_portion);

    strcat(file_path, ".csv");
    if(write_result(file_path) == -1) {
        /* return error */
        exit(0);
    }
#endif

    // pthread_mutex_destroy(&mutex);
    // pthread_cond_destroy(&cond);

    return 0;

}
#else

void gpu_accel_reverse(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int theoretical_exp, int theo_thread, int ext_output, int save_labels, char *outfile, int letter_box, int benchmark_layers)
{
    printf("!!ERROR!! GPU = 0 \n");
}
#endif  // GPU

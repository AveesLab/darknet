#include <stdlib.h>
#include "darknet.h"
#include "network.h"
#include "parser.h"
#include "detector.h"
#include "option_list.h"

#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#ifdef OPENBLAS
#include <cblas.h>
#endif

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

#ifdef MULTI_PROCESSOR

static int sem_id;
static key_t key = 1234;

typedef struct process_data_t{
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
    int process_id;

#ifndef MEASURE
    double execution_time[200];
    double frame_rate[200];
#endif

} process_data_t;

#ifdef MEASURE
typedef struct measure_data_t{
    double start_preprocess[200];
    double end_preprocess[200];
    double e_preprocess[200];

    double start_infer[200];
    double start_gpu_waiting[200];
    double start_gpu_infer[200];
    double end_gpu_infer[200];
    double start_cpu_infer[200];
    double end_infer[200];

    double waiting_gpu[200];
    double e_gpu_infer[200];
    double e_cpu_infer[200];
    double e_infer[200];

    double start_postprocess[200];
    double end_postprocess[200];
    double e_postprocess[200];

    double execution_time[200];
    double frame_rate[200];

} measure_data_t;
#endif

#ifdef MEASURE
static int write_result(char *file_path, measure_data_t *measure_data) 
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

    fprintf(fp, "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", 
            "core_id", 
            "start_preprocess", "e_preprocess", "end_preprocess", 
            "start_infer", 
            "start_gpu_waiting", "waiting_gpu", 
            "start_gpu_infer", "e_gpu_infer", "end_gpu_infer", 
            "start_cpu_infer", "e_cpu_infer", "end_infer", 
            "e_infer",
            "start_postprocess", "e_postprocess", "end_postprocess", 
            "execution_time", "frame_rate");

    for(i = 0; i < num_exp * num_process; i++)
    {
        int core_id = (i + 1) - (i / num_process) * num_process;
        int count = i / num_process;

        fprintf(fp, "%d,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f,%0.2f\n",  
                core_id, 
                measure_data[core_id - 1].start_preprocess[count],   measure_data[core_id - 1].e_preprocess[count],   measure_data[core_id - 1].end_preprocess[count], 
                measure_data[core_id - 1].start_infer[count], 
                measure_data[core_id - 1].start_gpu_waiting[count],  measure_data[core_id - 1].waiting_gpu[count],
                measure_data[core_id - 1].start_gpu_infer[count],    measure_data[core_id - 1].e_gpu_infer[count],    measure_data[core_id - 1].end_gpu_infer[count],
                measure_data[core_id - 1].start_cpu_infer[count],    measure_data[core_id - 1].e_cpu_infer[count],    measure_data[core_id - 1].end_infer[count], 
                measure_data[core_id - 1].e_infer[count], 
                measure_data[core_id - 1].start_postprocess[count],  measure_data[core_id - 1].e_postprocess[count],  measure_data[core_id - 1].end_postprocess[count], 
                measure_data[core_id - 1].execution_time[count],     measure_data[core_id - 1].frame_rate[count]);
    }
    
    fclose(fp);

    return 1;
}
#endif

static union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static void lock_resource(int resource_num) {
    struct sembuf operations[1];
    operations[0].sem_num = resource_num;  
    operations[0].sem_op = -1;  
    operations[0].sem_flg = 0;  

    if (semop(sem_id, operations, 1) == -1) {
        perror("semop - lock_resource");
        exit(1);
    }
}

static void unlock_resource(int resource_num) {
    struct sembuf operations[1];
    operations[0].sem_num = resource_num;  
    operations[0].sem_op = 1;   
    operations[0].sem_flg = 0;  

    if (semop(sem_id, operations, 1) == -1) {
        perror("semop - unlock_resource");
        exit(1);
    }
}

#ifdef GPU
#ifdef MEASURE
static void processFunc(process_data_t data, int write_fd)
#else
static void processFunc(process_data_t data)
#endif
{

#ifdef MEASURE
    measure_data_t measure_data;
#endif

    // __CPU AFFINITY SETTING__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(data.process_id, &cpuset); // cpu core index
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
    extern gpu_yolo;

    network net = parse_network_cfg_custom(data.cfgfile, 1, 1, device); // set batch=1
    layer l = net.layers[net.n - 1];

    if (data.weightfile) {
        load_weights(&net, data.weightfile);
    }
    if (net.letter_box) data.letter_box = 1;
    net.benchmark_layers = data.benchmark_layers;
    fuse_conv_batchnorm(net);
    calculate_binary_weights(net);

    srand(2222222);

    if (data.filename) strncpy(input, data.filename, 256);
    else printf("Error! File is not exist.");

    for (i = 0; i < num_exp; i++) {

#ifdef NVTX
        char task[100];
        sprintf(task, "Task (cpu: %d)", data.process_id);
        nvtxRangeId_t nvtx_task;
        nvtx_task = nvtxRangeStartA(task);
#endif

#ifdef MEASURE
        // printf("\nProcess %d is set to CPU core %d count(%d) : %d \n\n", data.process_id, sched_getcpu(), data.process_id, i);
#else
        printf("\nProcess %d is set to CPU core %d\n\n", data.process_id, sched_getcpu());
#endif

        time = get_time_in_ms();
        // __Preprocess__
        lock_resource(0);
#ifdef MEASURE
        measure_data.start_preprocess[i] = get_time_in_ms();
#endif
        im = load_image(input, 0, 0, net.c);
        resized = resize_min(im, net.w);
        cropped = crop_image(resized, (resized.w - net.w)/2, (resized.h - net.h)/2, net.w, net.h);
        X = cropped.data;

#ifdef MEASURE
        measure_data.end_preprocess[i] = get_time_in_ms();
        measure_data.e_preprocess[i] = measure_data.end_preprocess[i] - measure_data.start_preprocess[i];
#endif

        // __Inference__
        // if (device) predictions = network_predict(net, X);
        // else predictions = network_predict_cpu(net, X);

#ifdef MEASURE
        measure_data.start_infer[i] = get_time_in_ms();
#endif

        if (net.gpu_index != cuda_get_device())
            cuda_set_device(net.gpu_index);
        int size = get_network_input_size(net) * net.batch;
        network_state state;
        state.index = 0;
        state.net = net;
        // state.input = X;
        state.input = net.input_state_gpu;
        memcpy(net.input_pinned_cpu, X, size * sizeof(float));
        state.truth = 0;
        state.train = 0;
        state.delta = 0;

#ifdef MEASURE
        measure_data.start_gpu_waiting[i] = get_time_in_ms();
#endif

        // GPU Inference

#ifdef NVTX
        char task_gpu[100];
        sprintf(task_gpu, "Task (cpu: %d) - GPU Inference", data.process_id);
        nvtxRangeId_t nvtx_task_gpu;
        nvtx_task_gpu = nvtxRangeStartA(task_gpu);
#endif

#ifdef MEASURE
        measure_data.start_gpu_infer[i] = get_time_in_ms();
#endif

        cuda_push_array(state.input, net.input_pinned_cpu, size);
        state.workspace = net.workspace;
        for(j = 0; j < gLayer; ++j){
            state.index = j;
            l = net.layers[j];
            if(l.delta_gpu && state.train){
                fill_ongpu(l.outputs * l.batch, 0, l.delta_gpu, 1);
            }

            l.forward_gpu(l, state);
            if (skip_layers[j]){
                cuda_pull_array(l.output_gpu, l.output, l.outputs * l.batch);
            }
            state.input = l.output_gpu;
        }

        cuda_pull_array(l.output_gpu, l.output, l.outputs * l.batch);
        state.input = l.output;

        CHECK_CUDA(cudaStreamSynchronize(get_cuda_stream()));

#ifdef MEASURE
        measure_data.end_gpu_infer[i] = get_time_in_ms();
#endif

#ifdef NVTX
        nvtxRangeEnd(nvtx_task_gpu);
#endif

        unlock_resource(0);

        // CPU Inference
#ifdef MEASURE
        measure_data.start_cpu_infer[i] = get_time_in_ms();
#endif

        state.workspace = net.workspace_cpu;
        gpu_yolo = 0;
        for(j = gLayer; j < net.n; ++j){
            state.index = j;
            l = net.layers[j];
            if(l.delta && state.train && l.train){
                scal_cpu(l.outputs * l.batch, 0, l.delta, 1);
            }
            l.forward(l, state);
            state.input = l.output;
        }

        if (gLayer == net.n) predictions = get_network_output_gpu(net);
        else predictions = get_network_output(net, 0);
        reset_wait_stream_events();
        //cuda_free(state.input);   // will be freed in the free_network()

#ifdef MEASURE
        measure_data.end_infer[i] = get_time_in_ms();
        measure_data.waiting_gpu[i] = measure_data.start_gpu_infer[i] - measure_data.start_gpu_waiting[i];
        measure_data.e_gpu_infer[i] = measure_data.end_gpu_infer[i] - measure_data.start_gpu_infer[i];
        measure_data.e_cpu_infer[i] = measure_data.end_infer[i] - measure_data.start_cpu_infer[i];
        measure_data.e_infer[i] = measure_data.end_infer[i] - measure_data.start_infer[i];
#endif

        // __Postprecess__
#ifdef MEASURE
        measure_data.start_postprocess[i] = get_time_in_ms();
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
        measure_data.end_postprocess[i] = get_time_in_ms();
        measure_data.e_postprocess[i] = measure_data.end_postprocess[i] - measure_data.start_postprocess[i];
        measure_data.execution_time[i] = measure_data.end_postprocess[i] - measure_data.start_preprocess[i];
        measure_data.frame_rate[i] = 1000 / measure_data.execution_time[i];
        
        // printf("\n%s: Predicted in %0.3f milli-seconds.\n", input, measure_data.e_infer[i]);
#else
        data.execution_time[i] = get_time_in_ms() - time;
        data.frame_rate[i] = 1000.0 / (data.execution_time[i] / num_process); // N process
        printf("\n%s: Predicted in %0.3f milli-seconds. (%0.3lf fps)\n", input, data.execution_time[i], measure_data.frame_rate[i]);
#endif
        // free memory
        free_image(im);
        free_image(resized);
        free_image(cropped);

#ifdef NVTX
        nvtxRangeEnd(nvtx_task);
#endif
    }

#ifdef MEASURE
    write(write_fd, &measure_data, sizeof(measure_data_t));
#endif

    // free memory
    free_detections(dets, nboxes);
    free_ptrs((void**)names, net.layers[net.n - 1].classes);
    free_list_contents_kvp(options);
    free_list(options);
    free_alphabet(alphabet);
    free_network(net);
    // free_network(net); // Error occur
}


void gpu_accel_mp(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int ext_output, int save_labels, char *outfile, int letter_box, int benchmark_layers)
{
    int i;

    pid_t pids[num_process];
    int status;

#ifdef MEASURE
    int fd[num_process][2];
#endif

    // Create semaphore set with NUM_PROCESSES semaphores
    sem_id = semget(key, 2, IPC_CREAT | 0666);

    if (sem_id == -1) {
        perror("semget");
        exit(1);
    }

    // Initialize semaphores
    union semun arg;
    unsigned short values[2] = {1, 1};  // Initialize both semaphores to 1
    arg.array = values;
    semctl(sem_id, 0, SETALL, arg);

    process_data_t data[num_process];

    for (i = 0; i < num_process; i++) {
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
        data[i].process_id = i + 1;
    }

    for (i = 0; i < num_process; i++) {

#ifdef MEASURE
        if (pipe(fd[i]) == -1) {
            perror("pipe");
            exit(1);
        }
#endif

        pids[i] = fork();
        if (pids[i] == 0) { // child process

#ifdef MEASURE
            close(fd[i][0]); // close reading end in the child
            processFunc(data[i], fd[i][1]);
            close(fd[i][1]);
#else
            processFunc(data[i]);
#endif

            exit(0);
        } else if (pids[i] < 0) {
            perror("fork");
            exit(1);
        }
    }

#ifdef MEASURE
    measure_data_t receivedData[num_process];

    // In the parent process, read data from all child processes
    for (i = 0; i < num_process; i++) {
        close(fd[i][1]); // close writing end in the parent
        read(fd[i][0], &receivedData[i], sizeof(measure_data_t));
        // data[i] = receivedData[i];
        close(fd[i][0]);
    }
#endif

    for (i = 0; i < num_process; i++) {
        wait(&status);
    }

    // Remove semaphores
    semctl(sem_id, 0, IPC_RMID);

#ifdef MEASURE
    printf("\n!!Write CSV File!! \n");
    char file_path[256] = "measure/";

    char* model_name = malloc(strlen(cfgfile) + 1);
    strncpy(model_name, cfgfile + 6, (strlen(cfgfile)-10));
    model_name[strlen(cfgfile)-10] = '\0';
    
    strcat(file_path, "gpu-accel-mp/");
    strcat(file_path, model_name);
    strcat(file_path, "/");

    strcat(file_path, "gpu-accel-mp_");

    char gpu_portion[20];
    sprintf(gpu_portion, "%03dglayer", gLayer);
    strcat(file_path, gpu_portion);

    strcat(file_path, ".csv");
    if(write_result(file_path, receivedData) == -1) {
        /* return error */
        exit(0);
    }
#endif

    return 0;

}
#else

void gpu_accel_mp(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int ext_output, int save_labels, char *outfile, int letter_box, int benchmark_layers)
{
    printf("!!ERROR!! GPU = 0 \n");
}
#endif  // GPU
#else

void gpu_accel_mp(char *datacfg, char *cfgfile, char *weightfile, char *filename, float thresh,
    float hier_thresh, int dont_show, int ext_output, int save_labels, char *outfile, int letter_box, int benchmark_layers)
{
    printf("!!ERROR!! MULTI_PROCESSOR = 0 \n");
}
#endif  // MULTI-PROCESSOR
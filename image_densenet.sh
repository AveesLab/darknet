
# ./darknet detector sequential ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -core_id 3 -num_exp 10

# ./darknet detector data-parallel ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_thread 3 -num_exp 10

# ./darknet detector data-parallel-mp ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_process 3 -num_exp 10

./darknet detector gpu-accel ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_thread 3 -glayer 100 -num_exp 30

# ./darknet detector gpu-accel-mp ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_process 3 -glayer 100 -num_exp 10

# ./darknet detector cpu-reclaiming ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_thread 3 -glayer 200 -rlayer 300 -num_exp 50

# ./darknet detector cpu-reclaiming-mp ./cfg/imagenet1k.data ./cfg/densenet201.cfg ./weights/densenet201.weights data/dog.jpg -num_process 3 -glayer 250 -rlayer 251 -num_exp 10


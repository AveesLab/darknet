import pandas as pd
import os

def gather_e_gpu_infer(directory, output_filename):
	# result를 담을 배열 정의
	results = ["e_gpu_infer"]
	file_count = 1

	for i in range(305):
		try:
			filename = os.path.join(directory, f"gpu-accel_{file_count:03d}glayer.csv")

			# 1. filename 확인
			# print(f"Checking {filename}...")

			if not os.path.exists(filename):
				print(f"{filename} does not exist!")
				break

			# 2. 파일 읽기 시도
			df = pd.read_csv(filename)
			# print(f"Successfully read {filename}")

			# 3. 'e_gpu_infer' 컬럼 확인
			if 'e_gpu_infer' not in df.columns:
				print(f"'e_gpu_infer' column not found in {filename}")
				break

			avg_e_gpu_infer = df['e_gpu_infer'].mean()

			# 4. avg_e_gpu_infer 값 출력
			# print(f"Average e_gpu_infer for {filename}: {avg_e_gpu_infer}")

			# result 배열에 avg_e_gpu_infer 추가하기
			results.append(avg_e_gpu_infer)

			file_count += 1

		except Exception as e:
			print(f"Error reading {filename} : {e}")
			break

	# result 배열을 csv 파일로 만들기
	with open(output_filename, 'w') as file:
		for value in results:
			file.write(str(value) + "\n")
		print(f"Successfully write {output_filename}")


directory = "/home/avees/baseline/darknet/measure/gpu-accel/densenet201/"
output_filename = "./measure/gpu_inference_list.csv"
gather_e_gpu_infer(directory, output_filename)

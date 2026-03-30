# Makefile for building Docker image

# Define the Dockerfile to use
DOCKERFILE = Dockerfile

# .env 文件的路径，可以根据实际情况调整
ENV_FILE := .env

# 如果 .env 文件存在，则将其内容导出为环境变量
ifeq ($(wildcard $(ENV_FILE)),$(ENV_FILE))
	include $(ENV_FILE)
	export $(shell sed 's/=.*//' $(ENV_FILE))
endif

# Define the Docker image name and tag
IMAGE_TAG = v$(DOCKER_IMAGE_VERSION)

# Build target
build:
	@echo "DOCKER_IMAGE_NAME: $(DOCKER_IMAGE_NAME)" # 这是一个命令，以制表符开头
	@echo "DOCKER_IMAGE_VERSION: $(DOCKER_IMAGE_VERSION)" # 另一个命令，以制表符开头
	@echo "Building Docker image: $(DOCKER_IMAGE_NAME):$(IMAGE_TAG) with Dockerfile: $(DOCKERFILE)"
	docker build -f $(DOCKERFILE) -t $(DOCKER_IMAGE_NAME):$(IMAGE_TAG) .

# Default target
.PHONY: build
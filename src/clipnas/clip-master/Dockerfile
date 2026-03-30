FROM pytorch/pytorch:2.5.0-cuda11.8-cudnn9-devel
# 如果还要改，可以基于0914版本的镜像进行更新，就不用重复打包模型了

# copy script and models_places dir into working directory
COPY service /workspace
COPY requirements.txt /workspace/requirements.txt

WORKDIR /workspace
RUN pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple

# script on docker started
CMD ["/bin/bash", "./on-docker-start.sh"]
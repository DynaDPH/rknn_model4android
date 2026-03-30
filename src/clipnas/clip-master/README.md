# Nas 图文向量化算法服务
## 手动部署
1. 更新VERSION.txt文件
2. 构建docker镜像
```bash
make build
```
3. 推送镜像
```bash
make push
```
4. 下载并启动镜像
```bash
bash ./download-docker-image.sh
bash ./start-docker.sh
```

## Auto Build(不推荐，依赖镜像下载速度很慢)
创建标签并推送至阿里云进行构建，例如：
```bash
git tag release-v20250721 feat/20250721 # git tag release-v$version feat/20250721
git push --tags
```
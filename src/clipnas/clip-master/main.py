import csv
import heapq

import torch
from PIL import Image
import datetime

import cn_clip.clip as clip
from cn_clip.clip import load_from_name, available_models
print("Available models:", available_models())  
# Available models: ['ViT-B-16', 'ViT-L-14', 'ViT-L-14-336', 'ViT-H-14', 'RN50']

device = "cuda" if torch.cuda.is_available() else "cpu"
print(device)
# model, preprocess = load_from_name("ViT-B-16", device=device, download_root='./')
model, preprocess = load_from_name('RN50', device=device, download_root='./service/models')
model.eval()

def predict(label_lst, raw_image):
    t = datetime.datetime.now()

    # text = clip.tokenize(["骑行", "步行","自行车","什么都不是"]).to(device)
    text = clip.tokenize(label_lst).to(device)
    # image = preprocess(Image.open("examples/riding.jpg")).unsqueeze(0).to(device)
    image = preprocess(raw_image).unsqueeze(0).to(device)

    print(t, datetime.datetime.now())

    with torch.no_grad():
        image_features = model.encode_image(image)
        text_features = model.encode_text(text)
        # 对特征进行归一化，请使用归一化后的图文特征用于下游任务
        image_features /= image_features.norm(dim=-1, keepdim=True)
        text_features /= text_features.norm(dim=-1, keepdim=True)

        logits_per_image, logits_per_text = model.get_similarity(image, text)
        print(logits_per_image, logits_per_text)
        probs = logits_per_image.softmax(dim=-1).cpu().numpy()

    print(t, datetime.datetime.now())
    return probs

def get_similarity(self, image, text):
    image_features = self.encode_image(image)
    text_features = self.encode_text(text)

    # normalized features
    image_features = image_features / image_features.norm(dim=1, keepdim=True)
    text_features = text_features / text_features.norm(dim=1, keepdim=True)

    # cosine similarity as logits
    logit_scale = self.logit_scale.exp()
    logits_per_image = logit_scale * image_features @ text_features.t()
    logits_per_text = logits_per_image.t()

    # shape = [global_batch_size, global_batch_size]
    return logits_per_image, logits_per_text
def read_csv_file(file_path):
    """
    读取CSV文件并解析成字典数组
    :param file_path: CSV文件路径
    :return: 包含字典的数组
    """
    result = []
    with open(file_path, 'r', encoding='utf-8') as csvfile:
        reader = csv.reader(csvfile)
        for row in reader:
            if len(row) == 3:  # 确保每行有三个部分
                id, en, cn = row
                result.append({
                    'id': id,
                    'en': en,
                    'cn': cn
                })
    return result

def top_k_values_and_indices(arr, k=5):
    """
    获取数组中前k大的值及其索引
    :param arr: 输入的浮点数数组
    :param k: 要获取的最大值数量
    :return: 包含(值, 索引)的列表，按值从大到小排序
    """
    if k > len(arr):
        k = len(arr)
    # 使用负值将最大堆转换为最小堆
    top_k = heapq.nlargest(k, enumerate(arr), key=lambda x: x[1])
    return [(value, index) for index, value in top_k]
def test1():
    label_lst = ["杰尼龟", "妙蛙种子", "小火龙", "皮卡丘", "雷丘", "黄色带电老鼠", "老鼠", "猫", "黄色", "都不是"]
    img = Image.open("examples/pokemon.jpeg")
    probs = predict(label_lst, img)
    print("Label probs:",top_k_values_and_indices(probs.tolist()[0], k=5))  # [[1.268734e-03 5.436878e-02 6.795761e-04 9.436829e-01]]


def test2():
    label_lst = ["杰尼龟", "妙蛙种子", "小火龙", "皮卡丘", "雷丘", "黄色带电老鼠", "老鼠", "猫", "黄色","城堡", "都不是"]
    img = Image.open("/Users/ycwei/Pictures/测试图片/500px_热门/ia_100000000.jpg")
    probs = predict(label_lst, img)
    print("Label probs:",top_k_values_and_indices(probs.tolist()[0], k=5))  # [[1.268734e-03 5.436878e-02 6.795761e-04 9.436829e-01]]


def test3():
    file_path = r'C:\Users\ycwei\Desktop\sjtu-develop\nas-gw\resource\places365-labels.csv'
    csv_data = read_csv_file(file_path)
    csv_data.append({"id":-1,"en":"其他","cn":"证件"})
    label_lst = [data["cn"] for data in csv_data]
    # img = Image.open("/Users/ycwei/Pictures/测试图片/下载(1)(1).png")
    img = Image.open("/Users/ycwei/Pictures/测试图片/下载(1)(1).png")
    probs = predict(label_lst, img)
    ranking = top_k_values_and_indices(probs.tolist()[0], k=5)
    print("probs and label:", [(r[0],csv_data[r[1]]) for r in ranking])  # [[1.268734e-03 5.436878e-02 6.795761e-04 9.436829e-01]]

def test4():
    label_lst = ["猫头鹰","其他","海洋"]
    imgs = [Image.open("/Users/ycwei/Pictures/测试图片/500px_热门/ia_100000002.jpg"),
           Image.open("/Users/ycwei/Pictures/测试图片/500px_热门/ia_100000003.jpg"),
           Image.open("/Users/ycwei/Pictures/测试图片/500px_热门/ia_100000009.jpg")]
    for img in imgs:
        probs = predict(label_lst, img)
        print(probs)

def test5():
    label_lst = [
        ""
    ]
    # label_lst = [
    #                 "地图导航",
    #                 "短信记录",
    #                 "表情包",
    #                 "聊天记录",
    #                 "订单",
    #                 "证件",
    #                 "转账记录",
    #                 "身份证",
    #                 "手写资料",
    #                 "收据",
    #                 "截图",
    #                 "通话记录",
    #                 "发票",
    #                 "银行卡",
    #                 "二维码",
    #                 "书籍",
    #                 "日程计划",
    #                 "金融理财",
    #                 "黑板",
    #                 "演示文稿(PPT)",
    #                 "户口本",
    #                 "驾驶证（驾照）",
    #                 "资格证书",
    #               ]
    imgs = [Image.open("/Users/ycwei/desktop/sjtu-develop/nas-gw/tmp/piao/发票 (1).jpg")]
    for img in imgs:
        probs = predict(label_lst, img)
        for i in range(len(label_lst)):
            print(label_lst[i], round( probs[0][i], 4))

if __name__ == '__main__':
    print(model.logit_scale.exp().item())
    test5()
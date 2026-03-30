from typing import List

import torch
import cn_clip.clip as clip
from PIL import Image
from cn_clip.clip import load_from_name, available_models

device = "cuda" if torch.cuda.is_available() else "cpu"
print(device)

# cuda
model_map = {
    'RN50': {
        "model": None,
        "preprocess": None
    },
    'ViT-B-16': {
        "model": None,
        "preprocess": None
    },
    # 'ViT-L-14': {
    #     "model": None,
    #     "preprocess": None
    # },
    # 'ViT-L-14-336':{
    #     "model": None,
    #     "preprocess": None
    # },
    'ViT-H-14': {
        "model": None,
        "preprocess": None
    },
}

model_options = list(model_map.keys())

def load_model(mdl_type):
    if mdl_type not in model_map:
        raise ValueError(f"Invalid model type: {mdl_type}. Supported types are: {list(model_map.keys())}")
    if model_map[mdl_type]["model"] is None or model_map[mdl_type]["preprocess"] is None:
        model, preprocess = load_from_name(mdl_type, device=device, download_root='./models')
        model.eval()
        model_map[mdl_type] = {
            "model": model,
            "preprocess": preprocess
        }
    return model_map[mdl_type]["model"], model_map[mdl_type]["preprocess"]

def is_gpu_available():
    return torch.cuda.is_available()

def vectorize_image(raw_image: Image, model_name: str):
    model, preprocess = load_model(model_name)
    image = preprocess(raw_image).unsqueeze(0).to(device)
    with torch.no_grad():
        image_features = model.encode_image(image)
        # 对特征进行归一化，请使用归一化后的图文特征用于下游任务
        image_features /= image_features.norm(dim=-1, keepdim=True)
    return image_features

def vectorize_text(text_lst: List[str], model_name: str):
    model, preprocess = load_model(model_name)
    text = clip.tokenize(text_lst).to(device)
    with torch.no_grad():
        text_features = model.encode_text(text)
        # 对特征进行归一化，请使用归一化后的图文特征用于下游任务
        text_features /= text_features.norm(dim=-1, keepdim=True)
    return text_features

def get_similarity(image: Image, text_lst: List[str], model_name: str):
    model, preprocess = load_model(model_name)
    image = preprocess(image).unsqueeze(0).to(device)
    text = clip.tokenize(text_lst).to(device)

    with torch.no_grad():
        logits_per_image, logits_per_text = model.get_similarity(image, text)
        probs = logits_per_image.softmax(dim=-1).cpu().numpy()
    return probs

def get_scale(model_name: str):
    model, preprocess = load_model(model_name)
    return model.logit_scale.exp().item()
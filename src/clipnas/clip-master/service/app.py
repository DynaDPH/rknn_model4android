import asyncio
import math
import time
from io import BytesIO

from typing import List
from fastapi import FastAPI, UploadFile, File, Form

from starlette.middleware.cors import CORSMiddleware
from lib.lib import vectorize_image, vectorize_text, get_similarity, get_scale, model_options
from json_mapping.response import ClipResponse,ClipVectorizationResponse
from PIL import Image


# Initialize FastAPI app
app = FastAPI()

# CORS configuration
origins = [
    "*"
]

# Add CORS middleware to the app
app.add_middleware(
    CORSMiddleware,
    allow_origins=origins,  # List of allowed origins
    allow_credentials=True,  # Allow credentials (cookies, authorization headers)
    allow_methods=["GET", "POST", "PUT", "DELETE"],  # Allowed HTTP methods
    allow_headers=["X-Custom-Header", "Content-Type"],  # Allowed headers
)


@app.post("/nas/api/v1/clip/compare")
async def clip_compare(
        images: List[UploadFile] = File(...),
        texts: List[str] = Form(...),
):
    # print(texts)
    t0 = time.time()

    # label_features = vectorize_text(texts)

    probs_lst = []
    # Read all images in parallel
    image_bytes_list = await asyncio.gather(*(image.read() for image in images))
    for i in range(len(image_bytes_list)):
        # image_features = vectorize_image(Image.open(BytesIO(image_bytes_list[i])).convert('RGB'))
        probs = get_similarity(Image.open(BytesIO(image_bytes_list[i])).convert('RGB'), texts)
        probs_lst.append(probs.tolist()[0])
    t1 = time.time()

    # print(probs_lst)

    return ClipResponse(
        prob_list=probs_lst,
        cost_time=t1 - t0,  # api cost_time
    )

@app.post("/nas/api/v1/clip/vectorize/image")
async def clip_vectorize_images(
        images: List[UploadFile] = File(...),
        mdl: str = Form(model_options[0], enum=model_options),
):
    t0 = time.time()
    v_list = []
    # Read all images in parallel
    image_bytes_list = await asyncio.gather(*(image.read() for image in images))
    for i in range(len(image_bytes_list)):
        # image_features = vectorize_image(Image.open(BytesIO(image_bytes_list[i])).convert('RGB'))
        vector = vectorize_image(Image.open(BytesIO(image_bytes_list[i])).convert('RGB'), mdl)
        v_list.append(vector.tolist()[0])
    t1 = time.time()

    # print(v_list)

    return ClipVectorizationResponse(
        vectors=v_list,
        cost_time=t1 - t0,  # api cost_time
        scale=get_scale(mdl)
    )

@app.post("/nas/api/v1/clip/vectorize/text")
async def clip_vectorize_texts(
        texts: List[str] = Form(...),
        mdl: str = Form(model_options[0], enum=model_options),
):
    t0 = time.time()

    v_list = vectorize_text(texts, mdl)

    t1 = time.time()
    return ClipVectorizationResponse(
        vectors=v_list,
        cost_time=t1 - t0,  # api cost_time
        scale=get_scale(mdl)
    )



if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=28893)

from pydantic import BaseModel, Field, ConfigDict

class ClipResponse(BaseModel):
    model_config = ConfigDict(
        populate_by_name=True,
        # arbitrary_types_allowed=True
    )  # Added arbitrary_types_allowed=True here as a quick fix, but see explanation

    prob_list: list[list[float]] = Field(alias="probList", description="Similarity scores")
    cost_time: float = Field("costTime", description="API cost time at server side")

class ClipVectorizationResponse(BaseModel):
    model_config = ConfigDict(
        populate_by_name=True,
        # arbitrary_types_allowed=True
    )  # Added arbitrary_types_allowed=True here as a quick fix, but see explanation

    vectors: list[list[float]] = Field(alias="vectors", description="Vector list (1024 dim)")
    cost_time: float = Field("costTime", description="API cost time at server side")
    scale: float = Field(alias="scale", description="Vector scale, a parameter for probs calculation")

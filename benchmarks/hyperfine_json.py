"""Read mean wall-clock seconds from hyperfine 1.x and 2.x exports."""


def wall_time_means(data: dict) -> list[float]:
    version = data.get("schema_version", 1)
    if version == 1:
        return [result["mean"] for result in data["results"]]
    if version == 2:
        means = []
        for result in data["results"]:
            wall = result["summary"]["time_wall_clock"]
            if wall.get("unit") != "second":
                raise ValueError(f"Unexpected hyperfine wall-time unit: {wall.get('unit')!r}")
            means.append(wall["mean"])
        return means
    raise ValueError(f"Unsupported hyperfine JSON schema version: {version!r}")

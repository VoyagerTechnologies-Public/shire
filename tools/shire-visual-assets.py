#!/usr/bin/env python3
"""Stage configured visualization assets for the GSW image without vendoring them."""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import shutil
import struct
import zlib

import yaml

ROOT = Path(__file__).resolve().parent.parent


def source_path(value: str) -> Path:
    path = Path(value).expanduser()
    path = path if path.is_absolute() else ROOT / path
    if not path.is_file():
        raise ValueError(f"visualization asset does not exist: {path}")
    return path


def material_colors(path: Path) -> dict[str, tuple[float, float, float]]:
    colors: dict[str, tuple[float, float, float]] = {}
    if not path.is_file():
        return colors
    current = ""
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "newmtl" and len(parts) > 1:
            current = parts[1]
        elif parts[0] == "Kd" and len(parts) >= 4 and current:
            colors[current] = tuple(float(v) for v in parts[1:4])
    return colors


def obj_to_glb(source: Path, destination: Path) -> None:
    """Convert OBJ geometry and diffuse colors to a self-contained glTF binary.

    Complex textured assets can be configured as an already-converted .glb.
    """
    vertices: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    colors: list[tuple[float, float, float]] = []
    material_rgb: dict[str, tuple[float, float, float]] = {}
    faces: dict[str, list[int]] = defaultdict(list)
    packed_pos: list[float] = []
    packed_normal: list[float] = []
    packed_color: list[float] = []
    unique: dict[tuple[int, int], int] = {}
    current_material = "default"

    for line in source.open(errors="replace"):
        parts = line.split()
        if not parts:
            continue
        kind = parts[0]
        if kind == "mtllib" and len(parts) > 1:
            material_rgb.update(material_colors(source.parent / parts[1]))
        elif kind == "v" and len(parts) >= 4:
            vertices.append(tuple(float(v) for v in parts[1:4]))
            colors.append(tuple(float(v) for v in parts[4:7]) if len(parts) >= 7
                          else (1.0, 1.0, 1.0))
        elif kind == "vn" and len(parts) >= 4:
            vector = tuple(float(v) for v in parts[1:4])
            length = math.sqrt(sum(v*v for v in vector)) or 1.0
            normals.append(tuple(v/length for v in vector))
        elif kind == "usemtl" and len(parts) > 1:
            current_material = parts[1]
        elif kind == "f" and len(parts) >= 4:
            polygon: list[int] = []
            for token in parts[1:]:
                fields = token.split("/")
                vertex = int(fields[0]); vertex = vertex-1 if vertex > 0 else len(vertices)+vertex
                normal = int(fields[2]) if len(fields) > 2 and fields[2] else 0
                normal = normal-1 if normal > 0 else len(normals)+normal if normal < 0 else -1
                if not 0 <= vertex < len(vertices) or normal >= len(normals):
                    raise ValueError(f"invalid OBJ face index in {source}")
                key = (vertex, normal)
                index = unique.get(key)
                if index is None:
                    index = len(unique)
                    unique[key] = index
                    # 42's OBJ is Z-up. Encode Y-up glTF, then declare
                    # Y-up and X-forward so Cesium restores the 42 body axes
                    # without its default extra Z-forward rotation.
                    x, y, z = vertices[vertex]
                    packed_pos.extend((x, z, -y))
                    nx, ny, nz = normals[normal] if normal >= 0 else (0.0, 0.0, 1.0)
                    packed_normal.extend((nx, nz, -ny))
                    packed_color.extend(colors[vertex])
                polygon.append(index)
            for i in range(1, len(polygon)-1):
                faces[current_material].extend((polygon[0], polygon[i], polygon[i+1]))
    if not packed_pos or not any(faces.values()):
        raise ValueError(f"OBJ has no mesh faces: {source}")

    binary = bytearray()
    views: list[dict] = []
    accessors: list[dict] = []

    def add(data: bytes, target: int, component: int, count: int,
            kind: str, limits: tuple[list[float], list[float]] | None = None) -> int:
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(data)
        view = len(views)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        accessor = len(accessors)
        item = {"bufferView": view, "componentType": component, "count": count, "type": kind}
        if limits:
            item["min"], item["max"] = limits
        accessors.append(item)
        return accessor

    count = len(packed_pos)//3
    components = [packed_pos[i::3] for i in range(3)]
    pos = add(struct.pack(f"<{len(packed_pos)}f", *packed_pos), 34962, 5126, count, "VEC3",
              ([min(v) for v in components], [max(v) for v in components]))
    normal = add(struct.pack(f"<{len(packed_normal)}f", *packed_normal), 34962,
                 5126, count, "VEC3")
    color = add(struct.pack(f"<{len(packed_color)}f", *packed_color), 34962,
                5126, count, "VEC3")
    primitives = []
    materials = []
    for name, indices in faces.items():
        if not indices:
            continue
        index = add(struct.pack(f"<{len(indices)}I", *indices), 34963,
                    5125, len(indices), "SCALAR", ([min(indices)], [max(indices)]))
        rgb = material_rgb.get(name, (0.8, 0.8, 0.8))
        materials.append({"name": name, "doubleSided": True,
                          "pbrMetallicRoughness": {"baseColorFactor": [*rgb, 1.0],
                                                   "metallicFactor": 0.0,
                                                   "roughnessFactor": 0.8}})
        primitives.append({"attributes": {"POSITION": pos, "NORMAL": normal,
                                          "COLOR_0": color}, "indices": index,
                           "material": len(materials)-1})
    gltf = {"asset": {"version": "2.0", "generator": "SHIRE OBJ build converter"},
            "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": primitives}], "materials": materials,
            "buffers": [{"byteLength": len(binary)}],
            "bufferViews": views, "accessors": accessors}
    metadata = json.dumps(gltf, separators=(",", ":")).encode()
    metadata += b" " * (-len(metadata) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    total = 12 + 8 + len(metadata) + 8 + len(binary)
    with destination.open("wb") as result:
        result.write(struct.pack("<4sII", b"glTF", 2, total))
        result.write(struct.pack("<I4s", len(metadata), b"JSON"))
        result.write(metadata)
        result.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        result.write(binary)


def ppm_to_png(source: Path, destination: Path) -> None:
    """Convert 42's raw P6 texture to a compressed PNG with stdlib only."""
    def chunk(target, kind: bytes, data: bytes) -> None:
        target.write(struct.pack(">I", len(data)) + kind + data)
        target.write(struct.pack(">I", zlib.crc32(kind + data)))

    with source.open("rb") as original, destination.open("wb") as target:
        if original.readline().strip() != b"P6":
            raise ValueError(f"not a raw RGB PPM: {source}")
        header = []
        while len(header) < 3:
            line = original.readline()
            if not line:
                raise ValueError(f"incomplete PPM header: {source}")
            header.extend(line.split(b"#", 1)[0].split())
        width, height, maximum = map(int, header[:3])
        if maximum != 255 or width < 1 or height < 1:
            raise ValueError(f"unsupported PPM format: {source}")
        target.write(b"\x89PNG\r\n\x1a\n")
        chunk(target, b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        encoder = zlib.compressobj(level=6)
        pending = bytearray()
        for _ in range(height):
            row = original.read(width * 3)
            if len(row) != width * 3:
                raise ValueError(f"truncated PPM: {source}")
            pending.extend(encoder.compress(b"\0" + row))
            if len(pending) > 1024 * 1024:
                chunk(target, b"IDAT", bytes(pending))
                pending.clear()
        pending.extend(encoder.flush())
        if pending:
            chunk(target, b"IDAT", bytes(pending))
        chunk(target, b"IEND", b"")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=str(ROOT / "build/build.yaml"))
    parser.add_argument("--output", required=True)
    parser.add_argument("--mission", required=True)
    parser.add_argument("--spacecraft", required=True)
    args = parser.parse_args()
    config = yaml.safe_load(Path(args.config).read_text())
    if (config.get("mission"), config.get("spacecraft")) != (args.mission, args.spacecraft):
        raise ValueError("build.yaml selection does not match the requested visualization build")
    spacecraft_entries = config.get("mission_cfg", {}).get("spacecraft", [])
    selected = next((entry for entry in spacecraft_entries
                     if entry.get("name") == args.spacecraft), None)
    if selected and selected.get("config_file"):
        spacecraft_file = ROOT / "cfg" / selected["config_file"]
        options = yaml.safe_load(spacecraft_file.read_text()).get("visualization", {})
    else:
        options = config.get("spacecraft_cfg", {}).get("visualization", {})
    model = source_path(options.get("model", "42/Model/stf1_red.obj"))
    up_axis = str(options.get("model_up_axis", "Y")).upper()
    forward_axis = str(options.get("model_forward_axis",
                                   "X" if model.suffix.lower() == ".obj" else "Z")).upper()
    if up_axis not in "XYZ" or forward_axis not in "XYZ" or up_axis == forward_axis:
        raise ValueError("model up and forward axes must be distinct X, Y, or Z")
    model_to_body = options.get("model_to_body", [
        [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]])
    if (not isinstance(model_to_body, list) or len(model_to_body) != 4 or
            any(not isinstance(row, list) or len(row) != 4 for row in model_to_body)):
        raise ValueError("model_to_body must be a row-major 4x4 matrix")
    try:
        model_to_body = [[float(value) for value in row] for row in model_to_body]
    except (TypeError, ValueError) as exc:
        raise ValueError("model_to_body must contain numeric values") from exc
    if not all(math.isfinite(value) for row in model_to_body for value in row):
        raise ValueError("model_to_body must contain finite values")
    if any(abs(a-b) > 1e-6 for a, b in zip(model_to_body[3], [0, 0, 0, 1])):
        raise ValueError("model_to_body must be an affine transform")
    rotation = [row[:3] for row in model_to_body[:3]]
    for i in range(3):
        for j in range(3):
            dot = sum(rotation[i][k]*rotation[j][k] for k in range(3))
            if abs(dot - (1 if i == j else 0)) > 1e-4:
                raise ValueError("model_to_body rotation must be orthonormal")
    determinant = (rotation[0][0]*(rotation[1][1]*rotation[2][2]-rotation[1][2]*rotation[2][1])
                   - rotation[0][1]*(rotation[1][0]*rotation[2][2]-rotation[1][2]*rotation[2][0])
                   + rotation[0][2]*(rotation[1][0]*rotation[2][1]-rotation[1][1]*rotation[2][0]))
    if determinant < 0.9999:
        raise ValueError("model_to_body rotation must be right-handed")
    earth = source_path(options.get("earth_imagery", "42/World/BlueMarbleNG4096.ppm"))
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    for staged in output.glob("earth-imagery.*"):
        staged.unlink()
    if earth.suffix.lower() == ".ppm":
        name = "earth-imagery.png"
        ppm_to_png(earth, output / name)
    elif earth.suffix.lower() in (".jpg", ".jpeg", ".png", ".webp"):
        name = "earth-imagery" + earth.suffix.lower()
        shutil.copy2(earth, output / name)
    else:
        raise ValueError("Earth imagery must be PPM, JPEG, PNG, or WebP")
    (output / "earth-imagery.json").write_text(json.dumps({"file": name}))
    (output / "model-asset.json").write_text(json.dumps({
        "sourceName": model.name,
        "upAxis": up_axis, "forwardAxis": forward_axis,
        "modelToBody": [value for row in model_to_body for value in row]}))
    if model.suffix.lower() == ".glb":
        shutil.copy2(model, output / "model.glb")
    elif model.suffix.lower() == ".obj":
        obj_to_glb(model, output / "model.glb")
    else:
        raise ValueError("visualization model must be a .glb or .obj file")
    print(f"[visualization] staged Earth imagery and {model} for {args.mission}/{args.spacecraft}")


if __name__ == "__main__":
    main()

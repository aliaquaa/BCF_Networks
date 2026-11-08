
import re
import json
import geopandas as gpd
import pandas as pd
from shapely.geometry import Point
from pathlib import Path

# ============================================================
# 1. FILE PATHS AND SETTINGS
# ============================================================

GPS_PATH = Path(
    input("enter path to gps geojson: ")
)

# Currently only accepts 1 log at a time since there should be few
# Possible TODO: loop through every log in the log directory and output geojsons for each
LOG_PATH = Path(
   input("enter path to network ping log: ") 
)

out_file_and_pth = input("enter full path and file name for output: " )

OUTPUT_PATH = Path(
    out_file_and_pth + ".geojson"
)

# Maximum permitted difference between log and GPS timestamps
MAX_MATCH_GAP_SECONDS = 10 # If matched record results are really bad, this can be changed a bit

# ============================================================
# 2. READ AND EXTRACT GPS TRACK
# ============================================================

print("Reading GPS track...")

with open(GPS_PATH, "r") as f:
    geojson = json.load(f)

gps_records = []

for feature in geojson["features"]:

    properties = feature.get("properties", {})
    geometry = feature.get("geometry")

    if geometry is None:
        continue

    if geometry["type"] == "MultiLineString":
        lines = geometry["coordinates"]

    elif geometry["type"] == "LineString":
        lines = [geometry["coordinates"]]

    else:
        continue

    for line in lines:
        for coords in line:

            if len(coords) < 4:
                continue

            lon, lat, alt, ts = coords[:4]

            record = {
                "gps_time": float(ts),
                "gps_longitude": lon,
                "gps_latitude": lat,
                "gps_altitude": alt,
                "geometry": Point(lon, lat)
            }

            # Preserve original GPS feature attributes
            for key, value in properties.items():
                record[f"track_{key}"] = value

            gps_records.append(record)

if not gps_records:
    raise ValueError("No GPS coordinates with timestamps were found.")

gdf_gps = gpd.GeoDataFrame(
    gps_records,
    geometry="geometry",
    crs="EPSG:4326"
)

gdf_gps = gdf_gps.sort_values("gps_time")

print(f"GPS points extracted: {len(gdf_gps)}")


# ============================================================
# 3. READ AND PARSE PING LOG
# ============================================================

print("\nReading ping log...")

# Matches timestamps such as 2026-11-01T20:22:45Z
timestamp_pattern = re.compile(
    r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z)"
)

seq_pattern = re.compile(r"icmp_seq[= ](\d+)")
latency_pattern = re.compile(r"time=([\d.]+)\s*ms")
ttl_pattern = re.compile(r"ttl[= ](\d+)")
bytes_pattern = re.compile(r"(\d+)\s+bytes from\s+([^:]+)")

log_records = []
untimestamped_lines = 0

with open(LOG_PATH, "r", encoding="utf-8", errors="replace") as f:

    for line_number, line in enumerate(f, start=1):

        line = line.rstrip("\n")

        timestamp_match = timestamp_pattern.search(line)

        # Skip lines without timestamps
        if not timestamp_match:
            untimestamped_lines += 1
            continue

        seq_match = seq_pattern.search(line)
        latency_match = latency_pattern.search(line)
        ttl_match = ttl_pattern.search(line)
        bytes_match = bytes_pattern.search(line)

        log_records.append({
            "log_timestamp": timestamp_match.group(1),
            "icmp_seq": int(seq_match.group(1)) if seq_match else None,
            "latency_ms": float(latency_match.group(1))
                if latency_match else None,
            "ttl": int(ttl_match.group(1)) if ttl_match else None,
            "bytes_received": int(bytes_match.group(1))
                if bytes_match else None,
            "source_ip": bytes_match.group(2).strip()
                if bytes_match else None,
            "log_line_number": line_number,
            "raw_log": line
        })


if not log_records:
    raise ValueError("No timestamped log records were found.")

df_log = pd.DataFrame(log_records)

# Convert ISO 8601 timestamps to UTC datetime
df_log["log_timestamp"] = pd.to_datetime(
    df_log["log_timestamp"],
    utc=True
)

# Convert to Unix seconds, retaining fractional seconds
df_log["log_time"] = (
    pd.to_numeric(df_log["log_timestamp"]) / 1000000
)

print(df_log.head())

df_log = df_log.sort_values("log_time")

print(f"Timestamped log records: {len(df_log)}")
print(f"Lines without timestamps: {untimestamped_lines}")


# ============================================================
# 4. MATCH LOG RECORDS TO NEAREST GPS POINT
# ============================================================

print("\nMatching log timestamps to GPS locations...")

# Keep only the GPS fields needed for matching
gps_match = gdf_gps.copy()

gps_match = gps_match[
    ["gps_time", "gps_longitude", "gps_latitude",
     "gps_altitude", "geometry"]
    + [c for c in gps_match.columns if c.startswith("track_")]
]

# Nearest timestamp matching
df_merged = pd.merge_asof(
    df_log.sort_values("log_time"),
    gps_match.sort_values("gps_time"),
    left_on="log_time",
    right_on="gps_time",
    direction="nearest",
    tolerance=MAX_MATCH_GAP_SECONDS
)

# Calculate the time difference between the matched records
df_merged["gps_offset_seconds"] = (
    df_merged["log_time"] - df_merged["gps_time"]
).abs()

df_merged["gps_matched"] = df_merged["geometry"].notna()

print("LOG TIME:")
print(df_log["log_time"].head())

print("GPS TIME:")
print(gps_match["gps_time"].head())


# ============================================================
# 5. CREATE OUTPUT GEODATAFRAME
# ============================================================

gdf_output = gpd.GeoDataFrame(
    df_merged,
    geometry="geometry",
    crs="EPSG:4326"
)

# Create output directory if it does not exist
OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)

# Export all timestamped log records.
# Unmatched records are retained with null geometry.
gdf_output.to_file(
    OUTPUT_PATH,
    driver="GeoJSON",
    index=False
)


# ============================================================
# 6. REPORT RESULTS
# ============================================================

matched = int(gdf_output["gps_matched"].sum())
unmatched = len(gdf_output) - matched

print("\n========== RESULTS ==========")
print(f"GPS points:             {len(gdf_gps)}")
print(f"Timestamped log records: {len(df_log)}")
print(f"Matched records:         {matched}")
print(f"Unmatched records:       {unmatched}")
print(f"Match tolerance:         {MAX_MATCH_GAP_SECONDS} seconds")
print(f"Output:                  {OUTPUT_PATH}")
print("=============================")


import json
import geopandas as gpd
import pandas as pd
from shapely.geometry import Point
import glob
import os
from pathlib import Path

# 1. File paths
geojson_path = Path(
    input("enter path to gps geojson: ")
)

csv_dir = Path(
   input("enter path to cloudflare test directory: ") 
)

csv_files = glob.glob(os.path.join(csv_dir, "*.csv"))

# 2. Read GeoJSON directly
with open(geojson_path, "r") as f:
    geojson = json.load(f)

# 3. Extract GPS coordinates
gps_records = []

for feature in geojson["features"]:
    properties = feature["properties"]
    geometry = feature["geometry"]

    if geometry["type"] == "MultiLineString":
        lines = geometry["coordinates"]
    elif geometry["type"] == "LineString":
        lines = [geometry["coordinates"]]
    else:
        continue

    for line in lines:
        for coords in line:
            lon, lat, alt, ts = coords[:4]

            record = properties.copy()
            record["longitude"] = lon
            record["latitude"] = lat
            record["altitude"] = alt
            record["gps_time"] = int(ts)
            record["geometry"] = Point(lon, lat)

            gps_records.append(record)

# 4. Create GPS GeoDataFrame
gdf_points = gpd.GeoDataFrame(
    gps_records,
    geometry="geometry",
    crs="EPSG:4326"
)

gdf_points["gps_time"] = pd.to_numeric(gdf_points["gps_time"])
gdf_points = gdf_points.sort_values("gps_time")

print("Total GPS points:", len(gdf_points))

all_matched_records = []

# 5. Process each CSV file
for csv_path in csv_files:

    print(f"\nProcessing: {os.path.basename(csv_path)}")

    df_csv = pd.read_csv(csv_path)

    # Convert timestamps from milliseconds to seconds
    df_csv["time"] = (
        pd.to_numeric(df_csv["time"]) / 1000
    ).round().astype("int64")

    df_csv = df_csv.sort_values("time")

    # 6. Match network measurements to nearest GPS point
    gdf_merged = pd.merge_asof(
        df_csv,
        gdf_points,
        left_on="time",
        right_on="gps_time",
        direction="nearest",
        tolerance=10
    )

    # 7. Remove records without a GPS match
    gdf_merged = gdf_merged.dropna(subset=["geometry"])

    # Convert to GeoDataFrame
    gdf_merged = gpd.GeoDataFrame(
        gdf_merged,
        geometry="geometry",
        crs="EPSG:4326"
    )

    all_matched_records.append(gdf_merged)

    # 8. Export using the original CSV filename
    filename = os.path.splitext(os.path.basename(csv_path))[0]
    output_path = os.path.join(csv_dir, filename + "_joined.geojson")

    gdf_merged.to_file(output_path, driver="GeoJSON")

    # 9. Report results
    print("Network measurements:", len(df_csv))
    print("Matched measurements:", len(gdf_merged))
    print("Saved:", output_path)

# Combine all matched records
gdf_all = pd.concat(all_matched_records, ignore_index=True)

gdf_all = gpd.GeoDataFrame(
    gdf_all,
    geometry="geometry",
    crs="EPSG:4326"
)

# Export one combined GeoJSON
gdf_all.to_file(
    "joined_output_bcfios.geojson",
    driver="GeoJSON"
)

print("Total matched measurements:", len(gdf_all))

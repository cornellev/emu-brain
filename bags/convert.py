import sqlite3
import pandas as pd
import rclpy
from rosidl_runtime_py.utilities import get_message
from rclpy.serialization import deserialize_message
import os
import sys

def main():
    rclpy.init()

    if len(sys.argv) < 3:
        print("Usage: python3 convert.py <path_to_db3> <output_csv>")
        sys.exit(1)

    db_path = sys.argv[1]
    output_csv = sys.argv[2]

    if not os.path.exists(db_path):
        raise FileNotFoundError(f"Bag file {db_path} not found.")

    conn = sqlite3.connect(db_path)

    # Load topics
    topics = pd.read_sql_query("SELECT id, name, type FROM topics", conn)
    topic_info = topics[topics["name"] == "/strain_gauge_456"].iloc[0]
    topic_id, topic_type = topic_info["id"], topic_info["type"]

    if topic_type != "spi_com/msg/StrainGauge":
        raise ValueError(f"Topic has type {topic_type}, expected spi_com/msg/StrainGauge")

    # Load all messages for this topic
    query = f"SELECT timestamp, data FROM messages WHERE topic_id={topic_id}"
    messages = pd.read_sql_query(query, conn)

    # Prepare message type
    msg_type = get_message(topic_type)

    rows = []
    for _, msg in messages.iterrows():
        deserialized = deserialize_message(msg["data"], msg_type)

        # ros_time_sec = deserialized.header.stamp.sec + deserialized.header.stamp.nanosec * 1e-9

        rows.append({
            # "ros_time_sec": ros_time_sec,
            # "frame_id": deserialized.header.frame_id,
            "timestamp": deserialized.timestamp,
            # "voltage": deserialized.voltage,
            # "current": deserialized.current,
            "strain_gauge_4": deserialized.sensor1,
            "strain_gauge_5": deserialized.sensor2,
            "strain_gauge_6": deserialized.sensor3,
        })

    df = pd.DataFrame(rows)
    df.to_csv(output_csv, index=False)
    print(f"Saved {len(df)} messages to {output_csv}")

if __name__ == "__main__":
    main()

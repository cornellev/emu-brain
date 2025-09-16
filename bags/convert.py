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
        print("Usage: python3 extract_electrical_state.py <path_to_db3> <output_csv>")
        sys.exit(1)

    db_path = sys.argv[1]
    output_csv = sys.argv[2]

    if not os.path.exists(db_path):
        raise FileNotFoundError(f"Bag file {db_path} not found.")

    conn = sqlite3.connect(db_path)

    # Load topics
    topics = pd.read_sql_query("SELECT id, name, type FROM topics", conn)
    topic_info = topics[topics["name"] == "/serial_msg"].iloc[0]
    topic_id, topic_type = topic_info["id"], topic_info["type"]

    if topic_type != "std_msgs/msg/String":
        raise ValueError(f"Topic /serial_msg has type {topic_type}, expected std_msgs/msg/String")

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
            "data": deserialized.data
        })

    df = pd.DataFrame(rows)
    df.to_csv(output_csv, index=False)
    print(f"Saved {len(df)} messages to {output_csv}")

if __name__ == "__main__":
    main()

string id # Environment Name
uint64 revision # This is the revision number so others know when it changes.

# This message contains information about the state, i.e. the positions of its joints and links
sensor_msgs/JointState joint_state
tesseract_msgs/TransformMap floating_joint_states

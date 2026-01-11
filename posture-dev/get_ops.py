import tensorflow as tf

# Load the model
interpreter = tf.lite.Interpreter(model_path="models/movenet_lightning_int8.tflite")
interpreter.allocate_tensors()

# Get all operators used in the model
nodes = interpreter._get_ops_details()
op_codes = set([node['op_name'] for node in nodes])

print("Required Resolvers for C++:")
for op in sorted(op_codes):
    # Convert TFLite Op Name to C++ Resolver Name
    # e.g., 'CONV_2D' -> 'AddConv2D()'
    cpp_name = "".join([word.capitalize() for word in op.split('_')])
    print(f"resolver.Add{cpp_name}();")
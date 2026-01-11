#!/usr/bin/env python3
"""
Script to inspect TFLite model tensor information.
Prints input/output tensor details including shapes, types, and quantization parameters.
"""

import sys
import numpy as np

try:
    import tensorflow as tf
except ImportError:
    print("Error: tensorflow is not installed.")
    print("Please install it with: pip install tensorflow")
    sys.exit(1)

def format_tensor_info(tensor_details, index, tensor_type="input"):
    """Format tensor information in the style shown online."""
    info = {
        'name': tensor_details.get('name', 'unknown'),
        'index': index,
        'shape': np.array(tensor_details['shape'], dtype=np.int32),
        'dtype': tensor_details['dtype'],
    }
    
    # Add shape_signature if available
    if 'shape_signature' in tensor_details and tensor_details['shape_signature'] is not None:
        info['shape_signature'] = np.array(tensor_details['shape_signature'], dtype=np.int32)
    
    # Add quantization parameters
    if 'quantization_parameters' in tensor_details:
        qp = tensor_details['quantization_parameters']
        scales = qp.get('scales', [])
        zero_points = qp.get('zero_points', [])
        
        if len(scales) > 0 and len(zero_points) > 0:
            info['quantization'] = (float(scales[0]), int(zero_points[0]))
            info['quantization_parameters'] = {
                'scales': np.array(scales, dtype=np.float32),
                'zero_points': np.array(zero_points, dtype=np.int32),
                'quantized_dimension': qp.get('quantized_dimension', 0)
            }
    
    # Add sparsity parameters (usually empty)
    info['sparsity_parameters'] = tensor_details.get('sparsity_parameters', {})
    
    return info

def inspect_model(model_path):
    """Inspect a TFLite model and print tensor information."""
    print(f"Inspecting model: {model_path}\n")
    print("=" * 80)
    
    # Load the model
    interpreter = tf.lite.Interpreter(model_path=model_path)
    interpreter.allocate_tensors()
    
    # Get input details
    input_details = interpreter.get_input_details()
    print("\nINPUT TENSORS:")
    print("-" * 80)
    
    for i, inp in enumerate(input_details):
        tensor_info = format_tensor_info(inp, inp['index'], "input")
        print(f"\nInput {i}:")
        for key, value in tensor_info.items():
            print(f"  {key}: {value}")
    
    # Get output details
    output_details = interpreter.get_output_details()
    print("\n\nOUTPUT TENSORS:")
    print("-" * 80)
    
    for i, out in enumerate(output_details):
        tensor_info = format_tensor_info(out, out['index'], "output")
        print(f"\nOutput {i}:")
        for key, value in tensor_info.items():
            print(f"  {key}: {value}")
    
    # Get model signature (if available)
    signature_list = interpreter.get_signature_list()
    if signature_list:
        print("\n\nMODEL SIGNATURES:")
        print("-" * 80)
        for signature_name in signature_list.keys():
            print(f"  {signature_name}")
    
    print("\n" + "=" * 80)

if __name__ == "__main__":
    model_path = "models/movenet_lightning_int8.tflite"
    
    if len(sys.argv) > 1:
        model_path = sys.argv[1]
    
    try:
        inspect_model(model_path)
    except FileNotFoundError:
        print(f"Error: Model file not found: {model_path}")
        sys.exit(1)
    except Exception as e:
        print(f"Error inspecting model: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

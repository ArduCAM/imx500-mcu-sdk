import struct
import math
import numpy as np
from apParams.fb.FBApParams import FBApParams
from utils import align_up
try:
    from logger import logger
except ImportError:
    class _SimpleLogger:
        def debug(self, msg):
            print(f"[DEBUG] {msg}")

        def info(self, msg):
            print(f"[INFO] {msg}")

        def warn(self, msg):
            print(f"[WARN] {msg}")
    logger = _SimpleLogger()
from performance_profiler import PROFILE_FUNCTION, PROFILE_SCOPE

class Dimension(object):
    """Class representing the dimension of a tensor.

    Attributes:
        id (int): The ID of the dimension.
        size (int): The size of the dimension.
        serialization_index (int): The serialization index of the dimension.
        padding (int): The padding applied to the dimension.
    """
    def __init__(self) -> None:
        self.id: int = 0
        self.size: int = 0
        self.serialization_index: int = 0
        self.padding: int = 0

class InputTensor(object):
    """Class representing an input tensor in a neural network.

    Attributes:
        id (int): The ID of the input tensor.
        name (str): The name of the input tensor.
        dimensions (list[Dimension]): The dimensions of the input tensor.
        shift (int): The shift applied to the input tensor.
        scale (float): The scale applied to the input tensor.
        format (int): The format of the input tensor (0: signed, 1: unsigned).
        data (np.ndarray): The data of the input tensor.
    """
    def __init__(self) -> None:
        self.id: int = 0
        self.name: str = ""
        self.dimensions: list[Dimension] = []
        self.shift: int = 0
        self.scale: float = 0.0
        self.format: int = 0
        self.data: np.ndarray = None

    def get_dimensions(self):
        """Returns the size of each dimension of the tensor."""
        dimensions = []
        for dim in self.dimensions:
            dimensions.append(dim.size)
        return dimensions

class OutputTensor(InputTensor):
    """Class representing an output tensor, inherited from InputTensor.

    Attributes:
        bits_per_element (int): The number of bits per element in the output tensor.
    """
    def __init__(self) -> None:
        super().__init__()
        self.bits_per_element: int = 0

class NNContext(object):
    """Class representing a neural network context.

    Attributes:
        id (int): The ID of the network.
        name (str): The name of the network.
        type (str): The type of the network.
        input_tensors (list[InputTensor]): The input tensors for the network.
        output_tensors (list[OutputTensor]): The output tensors for the network.
    """
    def __init__(self) -> None:
        self.id: int = 0
        self.name: str = ""
        self.type: str = ""
        self.input_tensors: list[InputTensor] = []
        self.output_tensors: list[OutputTensor] = []
    
    def _to_dict(self) -> dict:
        """Converts the network object to a dictionary representation."""
        return {
            "id": self.id,
            "name": self.name,
            "type": self.type,
            "input_tensors": [self._tensor_to_dict(tensor) for tensor in self.input_tensors],
            "output_tensors": [self._tensor_to_dict(tensor, is_output=True) for tensor in self.output_tensors],
        }

    def _tensor_to_dict(self, tensor: InputTensor, is_output=False) -> dict:
        """Converts an input tensor object to a dictionary."""
        base = {
            "id": tensor.id,
            "name": tensor.name,
            "dimensions": [self._dimension_to_dict(dim) for dim in tensor.dimensions],
            "data": tensor.data.tolist() if isinstance(tensor.data, np.ndarray) else None,
        }
        if is_output:
            base["bits_per_element"] = getattr(tensor, "bits_per_element", 0)
        return base

    def _dimension_to_dict(self, dim: Dimension) -> dict:
        """Converts a dimension object to a dictionary."""
        return {
            "id": dim.id,
            "size": dim.size,
        }

class IMX500OutputParser:
    """Parser for IMX500 output data.

    This class provides methods for parsing DNN data and handling tensor data.
    """
    # A map that associates format and bits per element to corresponding NumPy data types.
    _data_type_map = {
        0: {
            8: np.int8,
            16: np.int16,
            32: np.int32
        },
        1: {
            8: np.uint8,
            16: np.uint16,
            32: np.uint32
        }
    }
    
    def __new__(cls, *args, **kwargs):
        """Prevent instantiation of this class."""
        raise TypeError(f"{cls.__name__} class cannot be instantiated")

    def _unpack_header(data):
        """Unpacks the header data from the IMX500 output."""
        result = struct.unpack('BBHHHB', data[:9])
        header = {
            "valid_flag": result[0],
            "frame_count": result[1],
            "max_length_of_line": result[2],
            "size_of_ap_parameter": result[3],
            "network_ordinal": result[4],
            "indicator": result[5]
        }
        return header

    @staticmethod
    @PROFILE_FUNCTION
    def split_frame(frame, img_width, img_height, data_in_y_channel=True):
        """Splits a frame into image data and neural network context data.

        Args:
            frame (np.ndarray): The raw frame data.
            img_width (int): The width of the image.
            img_height (int): The height of the image.
            data_in_y_channel (bool): Whether the DNN data is in the Y channel (True) or the UV channel (False).

        Returns:
            tuple: A tuple containing the image data and the neural network context data.
        """
        data = frame.reshape(-1)
        raw_img_size = int(img_width * 2 * img_height)

        logger.debug("width:{}, height:{}".format(img_width, img_height))

        img_data = data[:raw_img_size]
        nn_context_data = data[raw_img_size:]

        nn_context_data = nn_context_data.reshape(-1)
        if data_in_y_channel:
            nn_context_data = nn_context_data[0::2]
        else:
            nn_context_data = nn_context_data[1::2]

        logger.debug("DNN Data shape: {}".format(nn_context_data.shape))

        return img_data, nn_context_data

    @staticmethod
    @PROFILE_FUNCTION
    def _parseApParams(data, verbose=False):
        """Parses the application parameters from the data."""
        ap_parameter = FBApParams.GetRootAsFBApParams(data, 0)
        
        networks = []
        if verbose:
            logger.debug(f"NNContext Length: {ap_parameter.NetworksLength()}")
        for i in range(ap_parameter.NetworksLength()):
            out_network = NNContext()
            network = ap_parameter.Networks(i)
            if verbose:
                logger.debug(f"NNContext {i}")
                logger.debug(f"NNContext Id: {network.Id()}, Name: {network.Name()}, Type: {network.Type()}")
            out_network.id = network.Id()
            out_network.name = network.Name()
            out_network.type = network.Type()

            if verbose:
                logger.debug("Input Tensor:")
                logger.debug(f"Input Tensor Length: {network.InputTensorsLength()}")
            for n in range(network.InputTensorsLength()):
                out_input_tensor = InputTensor()
                input_tensor = network.InputTensors(n)
                if verbose:
                    logger.debug(f"Input Tensor {n}:")
                    logger.debug(f"\tId: {input_tensor.Id()}")
                    logger.debug(f"\tName: {input_tensor.Name()}")
                    logger.debug(f"\tShift: {input_tensor.Shift()}")
                    logger.debug(f"\tScale: {input_tensor.Scale()}")
                    logger.debug(f"\tFormat: {input_tensor.Format()}")
                out_input_tensor.id = input_tensor.Id()
                out_input_tensor.name = input_tensor.Name()
                out_input_tensor.shift = input_tensor.Shift()
                out_input_tensor.scale = input_tensor.Scale()
                out_input_tensor.format = input_tensor.Format()
                if verbose:
                    logger.debug(f"Input Tensor Dimensions Length: {input_tensor.DimensionsLength()}")
                for d in range(input_tensor.DimensionsLength()):
                    out_dimension = Dimension()
                    dim = input_tensor.Dimensions(d)
                    if verbose:
                        logger.debug(f"\t{dim.Id()}, {dim.Size()}, {dim.SerializationIndex()}, {dim.Padding()}")
                    out_dimension.id = dim.Id()
                    out_dimension.size = dim.Size()
                    out_dimension.serialization_index = dim.SerializationIndex()
                    out_dimension.padding = dim.Padding()

                    out_input_tensor.dimensions.append(out_dimension)
                out_network.input_tensors.append(out_input_tensor)
            if verbose:
                logger.debug("Output Tensor:")
                logger.debug(f"Output Tensor Length: {network.OutputTensorsLength()}")
            for n in range(network.OutputTensorsLength()):
                out_output_tensor = OutputTensor()

                output_tensor = network.OutputTensors(n)
                if verbose:
                    logger.debug(f"Output Tensor {n}:")
                    logger.debug(f"\tId: {output_tensor.Id()}")
                    logger.debug(f"\tName: {output_tensor.Name()}")
                    logger.debug(f"\tShift: {output_tensor.Shift()}")
                    logger.debug(f"\tScale: {output_tensor.Scale()}")
                    logger.debug(f"\tFormat: {output_tensor.Format()}")
                    logger.debug(f"\tBitsPerElement: {output_tensor.BitsPerElement()}")

                out_output_tensor.id = output_tensor.Id()
                out_output_tensor.name = output_tensor.Name()
                out_output_tensor.shift = output_tensor.Shift()
                out_output_tensor.scale = output_tensor.Scale()
                out_output_tensor.format = output_tensor.Format()
                out_output_tensor.bits_per_element = output_tensor.BitsPerElement()
                if verbose:
                    logger.debug(f"Output Tensor Dimensions Length: {output_tensor.DimensionsLength()}")
                for d in range(output_tensor.DimensionsLength()):
                    out_dimension = Dimension()
                    dim = output_tensor.Dimensions(d)
                    if verbose:
                        logger.debug(f"\t{dim.Id()}, {dim.Size()}, {dim.SerializationIndex()}, {dim.Padding()}")
                    out_dimension.size = dim.Size()
                    out_dimension.serialization_index = dim.SerializationIndex()
                    out_dimension.padding = dim.Padding()
                    out_output_tensor.dimensions.append(out_dimension)
                out_network.output_tensors.append(out_output_tensor)
            networks.append(out_network)

        return networks

    @staticmethod
    @PROFILE_FUNCTION
    def parse_imx500_nn_context(nn_context_data, pixel_per_line, input_tensor_line_num, output_tensor_line_num, is_only_framework=False):
        """Parses IMX500 neural network context data and reconstructs input/output tensors.

        This method extracts and reconstructs input and output tensor data from 
        the raw context data output.

        Args:
            nn_context_data (np.ndarray): Raw context buffer data from IMX500 output.
            pixel_per_line (int): Number of pixels per line in the output.
            is_only_framework (bool, optional): If True, only parses tensor metadata
                without extracting actual input/output data. Defaults to False.

        Returns:
            tuple: header(dict): input tensor and output tensor header dict.
                networks (list): list of NNContext objects.
        """
        
        header = {"input_tensor_header": None, "output_tensor_header": None}
        nn_context_data = np.ascontiguousarray(nn_context_data.reshape((-1, pixel_per_line)))
        input_tensor_header = IMX500OutputParser._unpack_header(nn_context_data[1])
        header["input_tensor_header"] = input_tensor_header
        logger.debug(f"input tensor header: {input_tensor_header}")

        if input_tensor_header["valid_flag"] != 1:
            logger.warning("Input Tensor Not Valid.")
        
        if input_tensor_header["max_length_of_line"] != pixel_per_line:
            raise RuntimeError("max_length_of_line({}) != pixel_per_line({})".format(input_tensor_header["max_length_of_line"], pixel_per_line))

        with PROFILE_SCOPE("imx500.parser.parse_ap_params"):
            networks = IMX500OutputParser._parseApParams(nn_context_data[1][12:], True)

        if len(networks[0].input_tensors) != 1:
            raise RuntimeError(f"Multiple Input Found. Expect 1 got {len(networks[0].input_tensors)}")
        
        input_dimension = networks[0].input_tensors[0].get_dimensions()

        if len(input_dimension) != 3:
            raise RuntimeError(f"Input Dimension Length != 3, got {len(input_dimension)}, {input_dimension}")

        input_tensor_width_real = input_dimension[1]
        input_tensor_width = align_up(input_tensor_width_real, 32)
        input_tensor_height = input_dimension[0]
        input_tensor_channel = input_dimension[2]
        input_tensor_size = input_tensor_width * input_tensor_height * input_tensor_channel

        # Calculate the theoretical expected size of nn_context_data.
        # Theoretical value = input data size + output data size,
        # where output size = (2/3) * input data size → total = (5/3) * input data size.
        expected_size = int(input_tensor_size / 3 * 5)

        # Check if the received data is smaller than expected.
        # If so, it may indicate that the model's input/output scale exceeds the supported limit,
        # resulting in incomplete data retrieval.
        if nn_context_data.size < expected_size:
            raise ValueError(
                f"Incomplete nn_context_data received. "
                f"Expected at least {expected_size}, but got {nn_context_data.size}. "
                f"This may indicate that the model's input/output scale exceeds the limit, "
                f"causing incomplete data retrieval."
            )
        
        # Extract Input Tensor (RGB Image)
        with PROFILE_SCOPE("imx500.parser.extract_input_tensor"):
            dnn_input_img = nn_context_data[2:, :pixel_per_line].reshape(-1)

            dnn_input_img = dnn_input_img[:input_tensor_size].reshape(input_tensor_channel, input_tensor_height, input_tensor_width)
            dnn_input_img = dnn_input_img.transpose(1, 2, 0)

            # Remove Padding
            dnn_input_img = dnn_input_img[:, :input_tensor_width_real]
        if not is_only_framework:
            networks[0].input_tensors[0].data = dnn_input_img.copy()

        output_tensor_data = nn_context_data[input_tensor_line_num + 1:, :pixel_per_line]
        output_tensor_header = IMX500OutputParser._unpack_header(output_tensor_data[0, :])
        header["output_tensor_header"] = output_tensor_header
        logger.debug(f"output tensor header: {output_tensor_header}")

        if output_tensor_header["valid_flag"] != 1:
            logger.warning("Output Tensor Data Not Valid.")

        # Postprocessing for output tensor (doing shift, scale, reshape and transpose)
        offset = 1
        with PROFILE_SCOPE("imx500.parser.extract_output_tensors"):
            for network_id in range(len(networks)):
                for op in networks[network_id].output_tensors:
                    dim = op.get_dimensions()
                    if is_only_framework:
                        continue
                    data_size = math.prod(dim) * math.ceil(op.bits_per_element / 8)

                    lines = math.ceil(data_size / pixel_per_line)
                    op_data = output_tensor_data[offset: offset + lines, :].copy().reshape(-1)[:data_size]

                    # Set data type
                    op_data.dtype = IMX500OutputParser._data_type_map[op.format][op.bits_per_element]

                    # Dequantization
                    op_data = (op_data - op.shift) * op.scale

                    dim.reverse()
                    op_data = op_data.reshape(dim)
                    op_data = np.transpose(op_data)

                    if output_tensor_header["valid_flag"] == 1:
                        op.data = op_data
                    else:
                        op.data = None
                    offset += lines

        return header, networks
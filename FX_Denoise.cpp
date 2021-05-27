#include <stdio.h>
#include <string.h>
#include <fstream>

#include <OpenImageDenoise/oidn.hpp>

#include "ufoFunctions.h"
#include "ufoProcess.h"
#include <vector>

//input ports
enum 
{
	INPUT_COLOR,
	INPUT_ALBEDO,
	INPUT_NORMAL,
	N_RASTER_IN  // the number of input ports
};

//output ports
enum 
{
	OUTPUT,
	N_RASTER_OUT  // the number of output ports
};

//node parameters
enum 
{
	PARAM_HDR,
	PARAM_SRGB,
	PARAM_AFFINITY,
	//PARAM_FILTER,  // 0 - RT, 1 - RTLightmap
	PARAM_CLEAN_SECONDARY_MAPS,
	//PARAM_PREFILTER_SECONDARY_MAPS,
	PARAM_COEFFITIENTS_PATH,  // string
	N_PARAM  // the number of node parameters
};

//node UI tabs
enum 
{
	GROUP_DENOISE,
	N_PARAM_GROUP  // number of tabs
};

enum 
{
	FILTER_RT,
	FILTER_RTLIGHTMAP,
	N_FILTER
};

//inner node data structure
typedef struct 
{
	int min_x, min_y, max_x, max_y;
} ProcessUserData;

//copy from oid
std::vector<char> loadFile(const std::string& filename, bool &is_correct)
{
	std::ifstream file(filename, std::ios::binary);
	if (file.fail())
	{
		is_correct = false;
		std::vector<char> buffer(0);
		return buffer;
	}
	file.seekg(0, file.end);
	const size_t size = file.tellg();
	file.seekg(0, file.beg);
	std::vector<char> buffer(size);
	file.read(buffer.data(), size);
	if (file.fail())
	{
		is_correct = false;
		buffer.resize(0);
		return buffer;
	}
	is_correct = true;
	return buffer;
}

std::vector<float> pixels;

//-----------------------------------
//-----------------------------------
//------------callbacks--------------

//define node parameters
ufoProcess ufoProcessDefine(void)
{
	//define enumerator
	char *filter_type[N_FILTER];
	filter_type[FILTER_RT] = "RT";
	filter_type[FILTER_RTLIGHTMAP] = "RT Lightmap";

	//initialize the node
	ufoProcess process_instance = ufoProcessCreate("FX Denoise", N_RASTER_IN, N_RASTER_OUT, N_PARAM, N_PARAM_GROUP);

	//create parameters tab
	ufoProcessParamGroupDefine(process_instance, GROUP_DENOISE, "UFODenoise",	"Denoise");

	//create input raster
	ufoProcessRasterInDefine(process_instance, INPUT_COLOR, "InputColor", "Input Color",	ufoRGBCompComb);
	ufoProcessRasterInDefine(process_instance, INPUT_ALBEDO, "InputAlbedo", "Input Albedo", ufoRGBCompComb);
	ufoProcessRasterInDefine(process_instance, INPUT_NORMAL, "InputNormal", "Input Normal", ufoRGBCompComb);

	ufoProcessSetRasterInOptional(process_instance, INPUT_ALBEDO, 1);
	ufoProcessSetRasterInOptional(process_instance, INPUT_NORMAL, 1);

	//create output raster
	ufoProcessRasterOutDefine(process_instance, OUTPUT,	"Output", "Output", 	ufoRGBCompComb);

	//parameters
	ufoProcessParamDefine(process_instance, PARAM_HDR, GROUP_DENOISE, "HDR", "HDR", ufoBooleanParam);
	ufoProcessSetParamDefaultValue(process_instance, PARAM_HDR, ufoDefaultChannelIndex, true);

	ufoProcessParamDefine(process_instance, PARAM_SRGB, GROUP_DENOISE, "SRGB", "sRGB", ufoBooleanParam);
	ufoProcessSetParamDefaultValue(process_instance, PARAM_SRGB, ufoDefaultChannelIndex, false);

	ufoProcessParamDefine(process_instance, PARAM_AFFINITY, GROUP_DENOISE, "Affinity", "Set Affinity", ufoBooleanParam);
	ufoProcessSetParamDefaultValue(process_instance, PARAM_AFFINITY, ufoDefaultChannelIndex, false);

	ufoProcessParamDefine(process_instance, PARAM_CLEAN_SECONDARY_MAPS, GROUP_DENOISE, "CleanSecondaryMaps", "Clean Secondary Maps", ufoBooleanParam);
	ufoProcessSetParamDefaultValue(process_instance, PARAM_CLEAN_SECONDARY_MAPS, ufoDefaultChannelIndex, false);

	//ufoProcessParamDefine(process_instance, PARAM_PREFILTER_SECONDARY_MAPS, GROUP_DENOISE, "PrefileterSecondaryMaps", "Prefilter Secondary Maps", ufoBooleanParam);
	//ufoProcessSetParamDefaultValue(process_instance, PARAM_PREFILTER_SECONDARY_MAPS, ufoDefaultChannelIndex, true);

	//ufoProcessEnumParamDefine(process_instance, PARAM_FILTER, GROUP_DENOISE, "FILTER", "Filter", N_FILTER, filter_type);
	//ufoProcessSetParamAnimAllow(process_instance, PARAM_FILTER, 0);

	ufoProcessParamDefine(process_instance, PARAM_COEFFITIENTS_PATH, GROUP_DENOISE, "WEIGHTS", "Weights Path", ufoStringParam);
	ufoProcessSetParamAnimAllow(process_instance, PARAM_COEFFITIENTS_PATH, 0);
	
	//pixel combination array
	const int number_combinations = 1;
	ufoPixelType input_combinations[N_RASTER_IN][1] =
	{
		{ ufoRGBFPixelType },{ ufoRGBFPixelType },{ ufoRGBFPixelType }
	};

	ufoPixelType output_combinations[N_RASTER_OUT][1] =
	{
		{ ufoRGBFPixelType },
	};
	//setup pixel combinations
	ufoProcessSetPixelTypeCombinations(process_instance, number_combinations, &input_combinations[0][0], &output_combinations[0][0], 0);

	return process_instance;
}

//default copy user data callback
void* ufoProcessCopyUserData(void *process_instance, void *user_data)
{
	ProcessUserData *new_data = 0;
	if (user_data)
	{
		new_data = static_cast<ProcessUserData *>(malloc(sizeof(ProcessUserData)));
		memcpy(new_data, user_data, sizeof(ProcessUserData));
	}

	return static_cast<void *>(new_data);
}

void ufoProcessDeleteUserData(void *process_instance, void *user_data)
{
	if (user_data)
	{
		free(user_data);
	}
}

void ufoProcessPreRender(void *process_instance, int x1, int y1, int x2, int y2)
{
	//init userdata if we need it
	ProcessUserData* user_data = static_cast<ProcessUserData *>(ufoProcessGetUserData(process_instance));
	if (!user_data)
	{
		user_data = static_cast<ProcessUserData *>(malloc(sizeof(ProcessUserData)));
		ufoProcessSetUserData(process_instance, static_cast<void*>(user_data));
	}

	//copy node parameters
	bool is_hdr = ufoProcessGetParamValue(process_instance, PARAM_HDR, ufoDefaultChannelIndex) > 0.5;
	bool is_srgb = ufoProcessGetParamValue(process_instance, PARAM_SRGB, ufoDefaultChannelIndex) > 0.5;
	bool use_affinity = ufoProcessGetParamValue(process_instance, PARAM_AFFINITY, ufoDefaultChannelIndex) > 0.5;
	bool clean_secondary = ufoProcessGetParamValue(process_instance, PARAM_CLEAN_SECONDARY_MAPS, ufoDefaultChannelIndex) > 0.5;  // if true, then albedo and normal are clean and we should't run prefiltering
	//bool prefilter_secondary = ufoProcessGetParamValue(process_instance, PARAM_PREFILTER_SECONDARY_MAPS, ufoDefaultChannelIndex) > 0.5;
	//int filter_mode = ufoProcessGetParamValue(process_instance, PARAM_FILTER, ufoDefaultChannelIndex);
	char *path = ufoProcessGetStringParamValue(process_instance, PARAM_COEFFITIENTS_PATH);
	bool use_weights = false;
	std::string weights_path = path;
	if (weights_path.size() > 0)
	{
		use_weights = true;
	}

	//calculate size of the image
	int width = x2 - x1 + 1;
	int height = y2 - y1 + 1;

	//read pixels from the input rasters
	ufoRaster color_in = ufoProcessGetRasterIn(process_instance, INPUT_COLOR);
	ufoRaster albedo_in = ufoProcessGetRasterIn(process_instance, INPUT_ALBEDO);
	ufoRaster normal_in = ufoProcessGetRasterIn(process_instance, INPUT_NORMAL);

	bool use_albedo = false;
	bool use_normal = false;
	std::vector<float> original_albedo;
	std::vector<float> original_normal;
	if(albedo_in != NULL)
	{
		use_albedo = true;
	}
	if(use_albedo && normal_in != NULL)
	{
		use_normal = true;
	}

	//get pixels pointers
	ufoPixelRGBF *color_pixel_pt = static_cast<ufoPixelRGBF *>(ufoRasterGetPixelAddress(color_in, x1, y1));
	ufoPixelRGBF *albedo_pixel_pt = NULL;
	ufoPixelRGBF *normal_pixel_pt = NULL;
	if(use_albedo)
	{
		albedo_pixel_pt = static_cast<ufoPixelRGBF *>(ufoRasterGetPixelAddress(albedo_in, x1, y1));
	}
	if(use_normal)
	{
		normal_pixel_pt = static_cast<ufoPixelRGBF *>(ufoRasterGetPixelAddress(normal_in, x1, y1));
	}

	//fill arrays by pixels values
	std::vector<float> original_pixels(width * height * 3);
	if(use_albedo)
	{
		original_albedo.resize(width * height * 3);
	}
	if(use_normal)
	{
		original_normal.resize(width * height * 3);
	}

	//init output array
	pixels.resize(width * height * 3);
	
	int index = 0;
	for (int y = y1; y <= y2; y++)
	{
		//int index = pixels.size() - 3*width * (row + 1);
		for (int x = x1; x <= x2; x++)
		{
			//read red channel
			original_pixels[index] = color_pixel_pt->red_;
			if(use_albedo)
			{
				original_albedo[index] = albedo_pixel_pt->red_;
			}
			if(use_normal)
			{
				original_normal[index] = normal_pixel_pt->red_;
			}
			index++;

			//read green channel
			original_pixels[index] = color_pixel_pt->green_;
			if (use_albedo)
			{
				original_albedo[index] = albedo_pixel_pt->green_;
			}
			if (use_normal)
			{
				original_normal[index] = normal_pixel_pt->green_;
			}
			index++;

			//read blue channel
			original_pixels[index] = color_pixel_pt->blue_;
			if (use_albedo)
			{
				original_albedo[index] = albedo_pixel_pt->blue_;
			}
			if (use_normal)
			{
				original_normal[index] = normal_pixel_pt->blue_;
			}
			index++;

			//increase iterators
			color_pixel_pt++;
			if (use_albedo)
			{
				albedo_pixel_pt++;
			}
			if (use_normal)
			{
				normal_pixel_pt++;
			}
		}
	}

	//start denoising
	//1. create device
	oidn::DeviceRef device = oidn::newDevice();
	device.set("setAffinity", use_affinity);
	device.commit();
	//2. add filter
	std::vector<char> weights;
	if (use_weights && !weights_path.empty())
	{
		bool is_correct = false;
		weights = loadFile(weights_path, is_correct);
		if (!is_correct)
		{
			use_weights = false;
			weights.clear();
			weights.shrink_to_fit();
		}
	}

	//oidn::FilterRef filter = filter_mode == 1 ? device.newFilter("RTLightmap") : device.newFilter("RT");
	oidn::FilterRef filter = device.newFilter("RT");  // use RT by default
	filter.setImage("color", original_pixels.data(), oidn::Format::Float3, width, height);
	if(use_albedo)
	{
		filter.setImage("albedo", original_albedo.data(), oidn::Format::Float3, width, height);
	}
	if(use_normal)
	{
		filter.setImage("normal", original_normal.data(), oidn::Format::Float3, width, height);
	}
	filter.setImage("output", pixels.data(), oidn::Format::Float3, width, height);
	filter.set("hdr", is_hdr);
	filter.set("srgb", is_srgb);
	filter.set("cleanAux", true);
	if (use_weights && !weights.empty())
	{
		filter.setData("weights", weights.data(), weights.size());
	}
	//prepare secondary filters
	oidn::FilterRef albedo_filter;
	oidn::FilterRef normal_filter;
	if (!clean_secondary && use_albedo)
	{
		albedo_filter = device.newFilter("RT"); // same filter type as for beauty
		albedo_filter.setImage("albedo", original_albedo.data(), oidn::Format::Float3, width, height);
		albedo_filter.setImage("output", original_albedo.data(), oidn::Format::Float3, width, height);
		albedo_filter.commit();
	}
	if (!clean_secondary && use_normal)
	{
		normal_filter = device.newFilter("RT"); // same filter type as for beauty
		normal_filter.setImage("normal", original_albedo.data(), oidn::Format::Float3, width, height);
		normal_filter.setImage("output", original_albedo.data(), oidn::Format::Float3, width, height);
		normal_filter.commit();
	}
	filter.commit();
	//3. execute
	if (!clean_secondary && use_albedo)
	{
		albedo_filter.execute();
	}
	if (!clean_secondary && use_normal)
	{
		normal_filter.execute();
	}
	filter.execute();
	//4. clear original pixels
	original_pixels.clear();
	original_pixels.shrink_to_fit();
	original_albedo.clear();
	original_albedo.shrink_to_fit();
	original_normal.clear();
	original_normal.shrink_to_fit();

	user_data->min_x = x1;
	user_data->min_y = y1;
	user_data->max_x = x2;
	user_data->max_y = y2;
	
	ufoProcessSetUserData(process_instance, static_cast<void*>(user_data));
}

void ufoProcessRenderLine(void *process_instance, int x1, int x2, int y)
{
	//get input and output rasters
	ufoRaster raster_in = ufoProcessGetRasterIn(process_instance, 0);
	ufoRaster raster_out = ufoProcessGetRasterOut(process_instance, 0);

	ProcessUserData* user_data = static_cast<ProcessUserData *>(ufoProcessGetUserData(process_instance));

	const ufoPixelType raster_in_type = ufoRasterGetPixelType(raster_in);
	const ufoPixelType raster_out_type = ufoRasterGetPixelType(raster_out);

	if (raster_in_type == raster_out_type)
	{
		if (raster_in_type == ufoRGBFPixelType)
		{
			ufoPixelRGBF *in_pixel_ptr = static_cast<ufoPixelRGBF *>(ufoRasterGetPixelAddress(raster_in, x1, y));
			ufoPixelRGBF *out_pixel_ptr = static_cast<ufoPixelRGBF *>(ufoRasterGetPixelAddress(raster_out, x1, y));
			const int start_index = (y*(user_data->max_x - user_data->min_x + 1) + x1) * 3;
			int shift = 0;
			for (int x = x1; x <= x2; x++)
			{
				if(y >= user_data->min_y && y <= user_data->max_y && x >= user_data->min_x && x <= user_data->max_x)
				{
					out_pixel_ptr->red_ = pixels[start_index + shift];
					shift++;
					out_pixel_ptr->green_ = pixels[start_index + shift];
					shift++;
					out_pixel_ptr->blue_ = pixels[start_index + shift];
					shift++;
				}
				else
				{
					out_pixel_ptr->red_ = 0.0f;
					out_pixel_ptr->green_ = 0.0f;
					out_pixel_ptr->blue_ = 0.0f;
				}
				in_pixel_ptr++;
				out_pixel_ptr++;
			}
		}
	}
}
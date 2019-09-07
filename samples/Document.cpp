﻿#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <chrono>
#include <utility>
#include <vector>
#include <sai.hpp>

#include "Benchmark.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

const char* const Help =
"Show .sai document information:\n"
"\tDocument (filenames)\n"
"\tWunkolo - Wunkolo@gmail.com";

void ProcessLayerFile(sai::VirtualFileEntry& LayerFile);
std::unique_ptr<std::uint32_t[]> ReadRasterLayer(
	const sai::LayerHeader& LayerHeader,
	sai::VirtualFileEntry& LayerFile
);

int main(int argc, char* argv[])
{
	if( argc < 2 )
	{
		std::puts(Help);
		return EXIT_FAILURE;
	}

	for( std::size_t i = 1; i < std::size_t(argc); ++i)
	{
		sai::Document CurDocument(argv[i]);

		if( !CurDocument.IsOpen() )
		{
			std::cout << "Error opening file for reading: " << argv[i] << std::endl;
			return EXIT_FAILURE;
		}

		const std::tuple<std::uint32_t,std::uint32_t> CanvasSize
			= CurDocument.GetCanvasSize();
		std::printf(
			"Width: %u Height: %u\n",
			std::get<0>(CanvasSize), std::get<1>(CanvasSize)
		);

		const auto Bench = Benchmark<std::chrono::nanoseconds>::Run(
			[&CurDocument]() -> void
			{
				CurDocument.IterateLayerFiles(
					[](sai::VirtualFileEntry& LayerFile)
					{
						ProcessLayerFile(LayerFile);
						return true;
					}
				);
				CurDocument.IterateSubLayerFiles(
					[](sai::VirtualFileEntry& SubLayerFile)
					{
						ProcessLayerFile(SubLayerFile);
						return true;
					}
				);
			}
		);
		std::printf(
			"Iterated Document of %s in %zu ns\n", argv[i], Bench.count()
		);
	}
	return EXIT_SUCCESS;
}

void ProcessLayerFile(
	sai::VirtualFileEntry& LayerFile
)
{
	using namespace sai::Literals;
	const sai::LayerHeader LayerHeader
		= LayerFile.Read<sai::LayerHeader>();
	std::printf("\t- \"%08x\"\n", LayerHeader.Identifier);

	char Name[256] = {};
	std::snprintf(Name, 256, "%08x", LayerHeader.Identifier);
    std::printf("\t\tType: %d ByteSize:%10d\n", LayerHeader.Type, int(LayerFile.GetSize()));
    std::printf("\t\tBounds: %8d, %8d, %8d, %8d\n", LayerHeader.Bounds.X, LayerHeader.Bounds.Y, LayerHeader.Bounds.Width, LayerHeader.Bounds.Height);
    std::printf("\t\tOpacity: %3d, Visible: %1x, Clipping: %1x, Preserve: %1x\n", LayerHeader.Opacity, LayerHeader.Visible, LayerHeader.Clipping, LayerHeader.PreserveOpacity);
    std::printf("\t\tBlending: %x\n", LayerHeader.Blending);
    std::printf("\t\tUnknown: %x, \tUnknown4: %x\n", LayerHeader.Unknown, LayerHeader.Unknown4);


	// Read serialization stream
	std::uint32_t CurTag;
	std::uint32_t CurTagSize;
	while( LayerFile.Read<std::uint32_t>(CurTag) && CurTag )
	{
		LayerFile.Read<std::uint32_t>(CurTagSize);
		switch( CurTag )
		{
            case "lorg"_Tag:
            {
                std::uint32_t Unknown0;
                std::uint32_t Unknown4;
                LayerFile.Read(Unknown0);
                LayerFile.Read(Unknown4);
                std::printf("\t\tUnknown: %d, \tUnknown4: %d\n", Unknown0, Unknown4);
                break;
            }
			case "name"_Tag:
			{
				char LayerName[256] = {};
				LayerFile.Read(LayerName, 256);
				std::printf("\t\tName: %.256s\n", LayerName);
				break;
            }
            case "pfid"_Tag:
            {
                std::uint32_t ParentSetID;
                LayerFile.Read(ParentSetID);
                std::printf("\t\tParentFolder: %d\n", ParentSetID);
                break;
            }
            case "plid"_Tag:
            {
                std::uint32_t ParentLayerID;
                LayerFile.Read(ParentLayerID);
                std::printf("\t\tParentLayer: %d\n", ParentLayerID);
                break;
            }
            case "lmfl"_Tag:
            {
                std::uint32_t ParentLayerID;
                LayerFile.Read(ParentLayerID);
                std::printf("\t\tMask Bitmask: %d\n",ParentLayerID);
                break;
            }
            case "fopn"_Tag:
            {
                std::uint8_t Open;
                LayerFile.Read(Open);
                std::printf("\t\tFolder Open: %d\n",Open);
                break;
            }
            case "texn"_Tag:
            {
            std::uint8_t TextureName[64] = {};
                LayerFile.Read(TextureName, 64);
                std::printf("\t\tTexture Name: %s\n", TextureName);
                break;
            }
            case "texp"_Tag:
            {
                std::uint16_t TextureScale;
                std::uint8_t TextureOpacity;
                LayerFile.Read(TextureScale);
                LayerFile.Read(TextureOpacity);
                std::printf("\t\tTexture Options, Scale: %3d, Opacity:%3d\n", TextureScale, TextureOpacity);
                break;
            }
            case "peff"_Tag:
            {
                std::uint8_t Enabled = 0; // bool
                std::uint8_t Opacity = 0; // 100
                std::uint8_t Width = 0;   // 1 - 15
                LayerFile.Read(Enabled);
                LayerFile.Read(Opacity);
                LayerFile.Read(Width);
                std::printf("\t\tFringe Effect Enabled: %d, Opacity: %3d, Width: %2d\n",Enabled, Opacity, Width);
                break;
            }
            case "vmrk"_Tag:
            {
                std::uint8_t Open;
                LayerFile.Read(Open);
                std::printf("\t\tvmrk: %d\n", Open);
                break;
            }
			default:
			{
				// for any streams that we do not handle,
				// we just skip forward in the stream
				LayerFile.Seek(LayerFile.Tell() + CurTagSize);
				break;
			}
		}
    }
	switch( static_cast<sai::LayerType>(LayerHeader.Type) )
	{
		case sai::LayerType::Layer:
		{
			if( auto LayerPixels = ReadRasterLayer(LayerHeader, LayerFile) )
			{
				stbi_write_png(
					(std::string(Name) + ".png").c_str(),
					LayerHeader.Bounds.Width, LayerHeader.Bounds.Height,
					4, LayerPixels.get(), 0
				);
			}
			break;
		}
		case sai::LayerType::Unknown4:
		case sai::LayerType::Linework:
		case sai::LayerType::Mask:
		case sai::LayerType::Unknown7:
		case sai::LayerType::Set:
		default:
			break;
    }
}


void RLEDecompressStride(
	std::uint8_t* Destination, const std::uint8_t *Source, std::size_t Stride,
	std::size_t StrideCount, std::size_t Channel
)
{
	Destination += Channel;
	std::size_t WriteCount = 0;

	while( WriteCount < StrideCount )
	{
		std::uint8_t Length = *Source++;
		if( Length == 128 ) // No-op
		{
		}
		else if( Length < 128 ) // Copy
		{
			// Copy the next Length+1 bytes
			Length++;
			WriteCount += Length;
			while( Length )
			{
				*Destination = *Source++;
				Destination += Stride;
				Length--;
			}
		}
		else if( Length > 128 ) // Repeating byte
		{
			// Repeat next byte exactly "-Length + 1" times
			Length ^= 0xFF;
			Length += 2;
			WriteCount += Length;
			std::uint8_t Value = *Source++;
			while( Length )
			{
				*Destination = Value;
				Destination += Stride;
				Length--;
			}
		}
	}
}

std::unique_ptr<std::uint32_t[]> ReadRasterLayer(
	const sai::LayerHeader& LayerHeader, sai::VirtualFileEntry& LayerFile
)
{
	const std::size_t TileSize = 32u;
	const std::size_t LayerTilesX = LayerHeader.Bounds.Width  / TileSize;
	const std::size_t LayerTilesY = LayerHeader.Bounds.Height / TileSize;

	// Read TileMap
	// Do not use a vector<bool> as this is commonly implemented as a specialized vector type that does not implement individual bool values as bytes but rather as packed bits within a word
	std::vector<std::uint8_t> TileMap;
	TileMap.resize(LayerTilesX * LayerTilesY);

	// Read Tile Map
	LayerFile.Read(TileMap.data(), LayerTilesX * LayerTilesY);

	// the resulting raster image data for this layer, RGBA 32bpp interleaved
	// Use a vector to ensure that tiles with no data are still initialized
	// to #00000000
	// Also note that the claim that SystemMax has made involving 16bit color depth
	// may actually only be true at run-time. All raster data found in files are stored at
	// 8bpc while only some run-time color arithmetic converts to 16-bit
	std::unique_ptr<std::uint32_t[]> LayerImage(
		new std::uint32_t[LayerHeader.Bounds.Width * LayerHeader.Bounds.Height]()
	);

	// iterate 32x32 tile chunks row by row
	for( std::size_t y = 0; y < LayerTilesY; ++y )
	{
		for( std::size_t x = 0; x < LayerTilesX; ++x )
		{
			 // Process active Tiles
			if( !TileMap[LayerTilesX * y + x] ) continue;
			// Decompress Tile
			std::array<std::uint8_t, 0x1000> CompressedTile;
			// 32 x 32 Tile of B8G8R8A8 pixels
			std::array<std::uint8_t, 0x1000> DecompressedTile;

			std::uint8_t CurChannel = 0;
			std::uint16_t RLESize = 0;
			// Iterate RLE streams for each channel
			while( LayerFile.Read<std::uint16_t>(RLESize) == sizeof(std::uint16_t) )
			{
				if( LayerFile.Read(CompressedTile.data(), RLESize) != RLESize )
				{
					// Error reading RLE stream
					break;
				}
				// Decompress and place into the appropriate interleaved channel
				RLEDecompressStride(
					DecompressedTile.data(), CompressedTile.data(),
					sizeof(std::uint32_t), 0x1000 / sizeof(std::uint32_t),
					CurChannel
				);
				++CurChannel;
				if( CurChannel >= 4 ) // skip all other channels besides the RGBA ones we care about
				{
					for( std::size_t i = 0; i < 4; i++ )
					{
						const std::uint16_t Size = LayerFile.Read<std::uint16_t>();
						LayerFile.Seek(LayerFile.Tell() + Size);
					}
					break;
				}
			}
			
			const auto Index2D = []
			(std::size_t X, std::size_t Y, std::size_t Stride) -> std::size_t
			{
				return X + (Y * Stride);
			};

			// Write 32x32 tile into final image
			const std::uint32_t* ImageSource
				= reinterpret_cast<const std::uint32_t*>(DecompressedTile.data());
			// Current 32x32 tile within final image
			std::uint32_t* ImageDest = LayerImage.get()
				+ Index2D(x * TileSize, y * LayerHeader.Bounds.Width, TileSize);
			for( std::size_t i = 0; i < (TileSize * TileSize); i++ )
			{
                std::uint32_t CurPixel = ImageSource[i];
				///
				// Do any Per-Pixel processing you need to do here
                ///

                std::uint8_t reorderdPixel[4];
                memcpy(reorderdPixel, &CurPixel, sizeof(CurPixel));
                std::swap<std::uint8_t>(reorderdPixel[0], reorderdPixel[2]);
                memcpy(&CurPixel, reorderdPixel, sizeof(CurPixel));

				ImageDest[
					Index2D(i % TileSize, i / TileSize, LayerHeader.Bounds.Width)
                ] = CurPixel;
			}
		}
	}
	return LayerImage;
}

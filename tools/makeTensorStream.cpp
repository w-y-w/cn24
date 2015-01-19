/*
 * This file is part of the CN24 semantic segmentation software,
 * copyright (C) 2015 Clemens-Alexander Brust (ikosa dot de at gmail dot com).
 *
 * For licensing information, see the LICENSE file included with this project.
 */
/**
 * \file makeTensorStream.cpp
 * \brief Tool to import datasets
 *
 * \author Clemens-Alexander Brust (ikosa dot de at gmail dot com)
 */

#include <iostream>
#include <fstream>

#include <cn24.h>

int main ( int argc, char** argv ) {
  if ( argc < 7 ) {
    LOGERROR << "USAGE: " << argv[0] << " <dataset config file> <image list file> <image directory> <label list file> <label directory> <output file>";
    LOGEND;
    return -1;
  }

  // Capture command line arguments
  std::string output_fname ( argv[6] );
  std::string label_directory ( argv[5] );
  std::string label_list_fname ( argv[4] );
  std::string image_directory ( argv[3] );
  std::string image_list_fname ( argv[2] );
  std::string dataset_config_fname ( argv[1] );

  Conv::System::Init();

  // Open dataset configuration files
  std::ifstream dataset_config_file ( dataset_config_fname,std::ios::in );

  if ( !dataset_config_file.good() ) {
    FATAL ( "Cannot open dataset configuration file!" );
  }

  // Load dataset
  Conv::TensorStreamDataset* dataset = Conv::TensorStreamDataset::CreateFromConfiguration ( dataset_config_file, true );
  unsigned int CLASSES = dataset->GetClasses();

  // Open file lists
  std::ifstream image_list_file ( image_list_fname, std::ios::in );

  if ( !image_list_file.good() ) {
    FATAL ( "Cannot open image list file!" );
  }

  std::ifstream label_list_file ( label_list_fname, std::ios::in );

  if ( !label_list_file.good() ) {
    FATAL ( "Cannot open label list file!" );
  }

  // Open output file
  std::ofstream output_file ( output_fname, std::ios::out | std::ios::binary );

  if ( !output_file.good() ) {
    FATAL ( "Cannot open output file!" );
  }

  // Iterate through lists
  while ( !image_list_file.eof() ) {
    std::string image_fname;
    std::string label_fname;
    std::getline ( image_list_file, image_fname );
    std::getline ( label_list_file, label_fname );

    if ( image_fname.length() < 5 || label_fname.length() < 5 )
      break;

    LOGINFO << "Importing files " << image_fname << " and " << label_fname << "...";
    Conv::Tensor image_tensor ( image_directory + image_fname );
    Conv::Tensor label_rgb_tensor ( label_directory + label_fname );

    if ( image_tensor.width() != label_rgb_tensor.width() ||
         image_tensor.height() != label_rgb_tensor.height() ) {
      LOGERROR << "Dimensions don't match, skipping file!";
      continue;
    }

    Conv::Tensor label_tensor ( 1, label_rgb_tensor.width(), label_rgb_tensor.height(), CLASSES );

    // Convert RGB images into multi-channel label tensors
    if ( CLASSES == 1 ) {
      const unsigned int foreground_color = dataset->GetClassColors() [0];
      const Conv::datum fr = DATUM_FROM_UCHAR ( ( foreground_color >> 16 ) & 0xFF ),
                        fg = DATUM_FROM_UCHAR ( ( foreground_color >> 8 ) & 0xFF ),
                        fb = DATUM_FROM_UCHAR ( foreground_color & 0xFF );

      for ( unsigned int y = 0; y < label_rgb_tensor.height(); y++ ) {
        for ( unsigned int x = 0; x < label_rgb_tensor.width(); x++ ) {
          Conv::datum lr, lg, lb;

          if ( label_rgb_tensor.maps() == 3 ) {
            lr = *label_rgb_tensor.data_ptr_const ( x,y,0,0 );
            lg = *label_rgb_tensor.data_ptr_const ( x,y,1,0 );
            lb = *label_rgb_tensor.data_ptr_const ( x,y,2,0 );
          } else if ( label_rgb_tensor.maps() == 1 ) {
            lr = *label_rgb_tensor.data_ptr_const ( x,y,0,0 );
            lg = lr;
            lb = lr;
          } else {
	    FATAL("Unsupported input channel count!");
	  }

          const Conv::datum class1_diff = std::sqrt ( ( lr - fr ) * ( lr - fr )
                                          + ( lg - fg ) * ( lg - fg )
                                          + ( lb - fb ) * ( lb - fb ) ) / std::sqrt(3.0);
          const Conv::datum val = 1.0 - 2.0 * class1_diff;
          *label_tensor.data_ptr ( x,y,0,0 ) = val;
        }
      }
    } else {
      FATAL ( "This code path is not yet implemented!" );
    }

    image_tensor.Serialize ( output_file );
    label_tensor.Serialize ( output_file );
  }

  LOGEND;
}

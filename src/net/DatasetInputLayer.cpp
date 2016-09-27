/*
 * This file is part of the CN24 semantic segmentation software,
 * copyright (C) 2015 Clemens-Alexander Brust (ikosa dot de at gmail dot com).
 *
 * For licensing information, see the LICENSE file included with this project.
 */

/**
* @file DatasetInputLayer.cpp
* @author Clemens-Alexander Brust (ikosa dot de at gmail dot com)
*/


#include <vector>
#include <array>
#include <random>
#include <algorithm>
#include <cstring>

#include "NetGraph.h"
#include "StatAggregator.h"
#include "Init.h"
#include "DatasetInputLayer.h"

namespace Conv {

DatasetInputLayer::DatasetInputLayer (Dataset* initial_dataset,
                                      const unsigned int batch_size,
                                      const datum loss_sampling_p,
                                      const unsigned int seed) :
  Layer(JSON::object()),
  active_dataset_(initial_dataset), batch_size_ (batch_size),
  loss_sampling_p_ (loss_sampling_p),
  generator_ (seed), dist_ (0.0, 1.0) {
  LOGDEBUG << "Instance created.";

  label_maps_ = active_dataset_->GetLabelMaps();
  input_maps_ = active_dataset_->GetInputMaps();

  if (seed == 0) {
    LOGWARN << "Random seed is zero";
  }

  if(active_dataset_->GetMethod() == FCN && active_dataset_->GetTask() == SEMANTIC_SEGMENTATION) {
    LOGDEBUG << "Using loss sampling probability: " << loss_sampling_p_;
  } else {
    loss_sampling_p_ = 1.0;
  }

  SetActiveDataset(active_dataset_);
}

void DatasetInputLayer::SetActiveDataset(Dataset *dataset) {
  active_dataset_ = dataset;
  LOGDEBUG << "Switching to dataset " << dataset->GetName();
  elements_training_ = dataset->GetTrainingSamples();
  elements_testing_ = dataset->GetTestingSamples();
  elements_total_ = elements_training_ + elements_testing_;

  LOGDEBUG << "Total samples: " << elements_total_;

  // Generate random permutation of the samples
  // First, we need an array of ascending numbers
  LOGDEBUG << "Generating random permutation..." << std::flush;
  perm_.clear();
  for (unsigned int i = 0; i < elements_training_; i++) {
    perm_.push_back (i);
  }

  RedoPermutation();
  current_element_testing_ = 0;
  current_element_ = 0;

  System::stat_aggregator->SetCurrentDataset(dataset->GetName());
}

bool DatasetInputLayer::CreateOutputs (const std::vector< CombinedTensor* >& inputs,
                                       std::vector< CombinedTensor* >& outputs) {
  if (inputs.size() != 0) {
    LOGERROR << "Inputs specified but not supported";
    return false;
  }

  if(active_dataset_->GetTask() == SEMANTIC_SEGMENTATION) {
    if (active_dataset_->GetMethod() == FCN) {
      CombinedTensor *data_output =
          new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                             active_dataset_->GetHeight(), input_maps_);

      CombinedTensor *label_output =
          new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                             active_dataset_->GetHeight(), label_maps_);

      CombinedTensor *helper_output =
          new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                             active_dataset_->GetHeight(), 2);

      CombinedTensor *localized_error_output =
          new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                             active_dataset_->GetHeight(), 1);

      outputs.push_back(data_output);
      outputs.push_back(label_output);
      outputs.push_back(helper_output);
      outputs.push_back(localized_error_output);
    } else if (active_dataset_->GetMethod() == PATCH) {
      CombinedTensor *data_output =
          new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                             active_dataset_->GetHeight(), input_maps_);

      CombinedTensor *label_output =
          new CombinedTensor(batch_size_, 1,
                             1, label_maps_);

      CombinedTensor *helper_output =
          new CombinedTensor(batch_size_, 1,
                             1, 2);

      CombinedTensor *localized_error_output =
          new CombinedTensor(batch_size_, 1,
                             1, 1);

      outputs.push_back(data_output);
      outputs.push_back(label_output);
      outputs.push_back(helper_output);
      outputs.push_back(localized_error_output);
    }
  } else if (active_dataset_->GetTask() == CLASSIFICATION) {
    CombinedTensor *data_output =
        new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                           active_dataset_->GetHeight(), input_maps_);

    CombinedTensor *label_output =
        new CombinedTensor(batch_size_, 1,
                           1, label_maps_);

    CombinedTensor *helper_output =
        new CombinedTensor(batch_size_, 1,
                           1, 2);

    CombinedTensor *localized_error_output =
        new CombinedTensor(batch_size_, 1,
                           1, 1);

    outputs.push_back(data_output);
    outputs.push_back(label_output);
    outputs.push_back(helper_output);
    outputs.push_back(localized_error_output);
  } else if(active_dataset_->GetTask() == DETECTION) {
    CombinedTensor *data_output =
        new CombinedTensor(batch_size_, active_dataset_->GetWidth(),
                           active_dataset_->GetHeight(), input_maps_);

    CombinedTensor *label_output =
        new CombinedTensor(batch_size_);

    CombinedTensor *helper_output =
        new CombinedTensor(batch_size_);

    CombinedTensor *localized_error_output =
        new CombinedTensor(0);

    DatasetMetadataPointer* metadata_buffer = new DatasetMetadataPointer[batch_size_];

    label_output->metadata = metadata_buffer;

    outputs.push_back(data_output);
    outputs.push_back(label_output);
    outputs.push_back(helper_output);
    outputs.push_back(localized_error_output);
  }
  return true;
}

bool DatasetInputLayer::Connect (const std::vector< CombinedTensor* >& inputs,
                                 const std::vector< CombinedTensor* >& outputs,
                                 const NetStatus* net) {
  UNREFERENCED_PARAMETER(net);
  // TODO validate
  CombinedTensor* data_output = outputs[0];
  CombinedTensor* label_output = outputs[1];
  CombinedTensor* helper_output = outputs[2];
  CombinedTensor* localized_error_output = outputs[3];

  if (data_output == nullptr || label_output == nullptr ||
      localized_error_output == nullptr)
    return false;

  bool valid = inputs.size() == 0 && outputs.size() == 4;

  if (valid) {
    data_output_ = data_output;
    label_output_ = label_output;
    helper_output_ = helper_output;
    localized_error_output_ = localized_error_output;
    if(active_dataset_->GetTask() == DETECTION)
      metadata_buffer_ = label_output_->metadata;
  }

  return valid;
}

void DatasetInputLayer::SelectAndLoadSamples() {
#ifdef BUILD_OPENCL
  data_output_->data.MoveToCPU (true);
  label_output_->data.MoveToCPU (true);
  localized_error_output_->data.MoveToCPU (true);
#endif

  for (std::size_t sample = 0; sample < batch_size_; sample++) {
    unsigned int selected_element = 0;
    bool force_no_weight = false;

    if (testing_) {
      // The testing samples are not randomized
      if (current_element_testing_ >= elements_testing_) {
        force_no_weight = true;
        selected_element = 0;
      } else {
        selected_element = current_element_testing_++;
      }

    } else {
      // Select samples until one from the right subset is hit
      // Select a sample from the permutation
      selected_element = perm_[current_element_];

      // Select next element
      current_element_++;

      // If this is is out of bounds, start at the beginning and randomize
      // again.
      if (current_element_ >= perm_.size()) {
        current_element_ = 0;
        RedoPermutation();
      }
    }

    // Copy image and label
    bool success;

    if (testing_)
      success = active_dataset_->GetTestingSample (data_output_->data, label_output_->data, helper_output_->data, localized_error_output_->data, sample, selected_element);
    else
      success = active_dataset_->GetTrainingSample (data_output_->data, label_output_->data, helper_output_->data, localized_error_output_->data, sample, selected_element);

    if (!success) {
      FATAL ("Cannot load samples from Dataset!");
    }

    if (!testing_ && !force_no_weight && active_dataset_->GetMethod() == FCN && active_dataset_->GetTask() == SEMANTIC_SEGMENTATION) {
      // Perform loss sampling
      const unsigned int block_size = 12;

      for (unsigned int y = 0; y < localized_error_output_->data.height(); y += block_size) {
        for (unsigned int x = 0; x < localized_error_output_->data.width(); x += block_size) {
          if (dist_ (generator_) > loss_sampling_p_) {
            for (unsigned int iy = y; iy < y + block_size && iy < localized_error_output_->data.height(); iy++) {
              for (unsigned int ix = x; ix < x + block_size && ix < localized_error_output_->data.width(); ix++) {
                *localized_error_output_->data.data_ptr (ix, iy, 0, sample) = 0;
              }
            }
          }
        }
      }
    }

    // Copy localized error
    if (force_no_weight)
      localized_error_output_->data.Clear (0.0, sample);

    // Load metadata
    if(active_dataset_->GetTask() == DETECTION) {
      if (testing_)
        success = active_dataset_->GetTestingMetadata(metadata_buffer_,sample, selected_element);
      else
        success = active_dataset_->GetTrainingMetadata(metadata_buffer_,sample, selected_element);
    }

    if (!success) {
      FATAL ("Cannot load metadata from Dataset!");
    }
  }
}

void DatasetInputLayer::FeedForward() {
  // Nothing to do here
}

void DatasetInputLayer::BackPropagate() {
  // No inputs, no backprop.
}

unsigned int DatasetInputLayer::GetBatchSize() {
  return batch_size_;
}

unsigned int DatasetInputLayer::GetLabelWidth() {
  return (active_dataset_->GetMethod() == PATCH || active_dataset_->GetTask() == CLASSIFICATION || active_dataset_->GetTask() == DETECTION) ? 1 : active_dataset_->GetWidth();
}

unsigned int DatasetInputLayer::GetLabelHeight() {
  return (active_dataset_->GetMethod() == PATCH || active_dataset_->GetTask() == CLASSIFICATION || active_dataset_->GetTask() == DETECTION) ? 1 : active_dataset_->GetHeight();
}

unsigned int DatasetInputLayer::GetSamplesInTestingSet() {
  return active_dataset_->GetTestingSamples();
}

unsigned int DatasetInputLayer::GetSamplesInTrainingSet() {
  return active_dataset_->GetTrainingSamples();
}

void DatasetInputLayer::RedoPermutation() {
  // Shuffle the array
  std::shuffle (perm_.begin(), perm_.end(), generator_);
}

void DatasetInputLayer::SetTestingMode (bool testing) {
  if (testing != testing_) {
    if (testing) {
      LOGDEBUG << "Enabled testing mode.";

      // Always test the same elements for consistency
      current_element_testing_ = 0;
    } else {
      LOGDEBUG << "Enabled training mode.";
    }
  }

  testing_ = testing;
}

void DatasetInputLayer::CreateBufferDescriptors(std::vector<NetGraphBuffer>& buffers) {
	NetGraphBuffer data_buffer;
	NetGraphBuffer label_buffer;
	NetGraphBuffer helper_buffer;
	NetGraphBuffer weight_buffer;
	data_buffer.description = "Data Output";
	label_buffer.description = "Label";
	helper_buffer.description = "Helper";
	weight_buffer.description = "Weight";
	buffers.push_back(data_buffer);
	buffers.push_back(label_buffer);
	buffers.push_back(helper_buffer);
	buffers.push_back(weight_buffer);
}

bool DatasetInputLayer::IsOpenCLAware() {
#ifdef BUILD_OPENCL
  return true;
#else
  return false;
#endif
}


}

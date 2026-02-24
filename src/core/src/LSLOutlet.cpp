#include "mccoutlet/LSLOutlet.hpp"

#include <cmath>

namespace mccoutlet {

LSLOutlet::LSLOutlet(const DeviceInfo& info)
    : info_(info)
{
    lsl::channel_format_t format;
    if (!info.scaled) {
        format = (info.resolution_bits <= 16) ? lsl::cf_int16 : lsl::cf_int32;
    } else {
        format = lsl::cf_float32;
    }

    lsl::stream_info stream_info(
        info.name,
        info.type,
        info.channel_count,
        info.sample_rate,
        format,
        info.source_id
    );

    lsl::xml_element desc = stream_info.desc();
    desc.append_child_value("manufacturer", "MeasurementComputing");

    // Acquisition metadata (always present)
    lsl::xml_element acq = desc.append_child("acquisition");
    if (info.resolution_bits > 0) {
        acq.append_child_value("resolution", std::to_string(info.resolution_bits));
    }
    acq.append_child_value("scaled", info.scaled ? "true" : "false");
    acq.append_child_value("range_min", std::to_string(info.range_min));
    acq.append_child_value("range_max", std::to_string(info.range_max));
    acq.append_child_value("range_label", info.range_label);

    if (!info.scaled && info.resolution_bits > 0) {
        double full_scale = std::pow(2.0, info.resolution_bits);
        double span = info.range_max - info.range_min;
        double slope = span / full_scale;
        double offset = (info.range_min + info.range_max) / 2.0;
        acq.append_child_value("scaling_slope", std::to_string(slope));
        acq.append_child_value("scaling_offset", std::to_string(offset));
    }

    // Channel metadata
    std::string unit = info.scaled ? "V" : "count";
    lsl::xml_element channels = desc.append_child("channels");
    for (int i = 0; i < info.channel_count; ++i) {
        lsl::xml_element ch = channels.append_child("channel");
        ch.append_child_value("label", "Ch" + std::to_string(i + 1));
        ch.append_child_value("unit", unit);
        ch.append_child_value("type", info.type);
    }

    outlet_ = std::make_unique<lsl::stream_outlet>(stream_info);
}

LSLOutlet::~LSLOutlet() = default;

void LSLOutlet::pushChunk(const std::vector<float>& data, double timestamp) {
    if (outlet_ && !data.empty()) {
        outlet_->push_chunk_multiplexed(data, timestamp);
    }
}

void LSLOutlet::pushChunk(const std::vector<int32_t>& data, double timestamp) {
    if (outlet_ && !data.empty()) {
        outlet_->push_chunk_multiplexed(data, timestamp);
    }
}

void LSLOutlet::pushChunk(const std::vector<int16_t>& data, double timestamp) {
    if (outlet_ && !data.empty()) {
        outlet_->push_chunk_multiplexed(data, timestamp);
    }
}

void LSLOutlet::pushSample(const std::vector<float>& sample) {
    if (outlet_ && !sample.empty()) {
        outlet_->push_sample(sample);
    }
}

std::string LSLOutlet::getStreamName() const {
    return info_.name;
}

bool LSLOutlet::hasConsumers() const {
    return outlet_ && outlet_->have_consumers();
}

} // namespace mccoutlet

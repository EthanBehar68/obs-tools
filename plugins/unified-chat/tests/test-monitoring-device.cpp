#include <doctest.h>

#include "core/monitoring-device.hpp"

using namespace unified_chat;

namespace {

const AudioDevice kHeadset{"Headphones (USB Audio)", "{0.0.0.00000000}.{new-id}"};
const AudioDevice kSpeakers{"Speakers (Realtek)", "{0.0.0.00000000}.{speakers}"};

} // namespace

TEST_CASE("Monitoring device: present, default or unset is fine")
{
	CHECK(CheckMonitoringDevice(kHeadset, {kSpeakers, kHeadset}).check == MonitoringCheck::Fine);
	CHECK(CheckMonitoringDevice({"Default", "default"}, {}).check == MonitoringCheck::Fine);
	CHECK(CheckMonitoringDevice({}, {kSpeakers}).check == MonitoringCheck::Fine);
}

TEST_CASE("Monitoring device: a new ID for the same name is reselected")
{
	const AudioDevice stale{kHeadset.name, "{0.0.0.00000000}.{old-id}"};
	const auto repair = CheckMonitoringDevice(stale, {kSpeakers, kHeadset});
	CHECK(repair.check == MonitoringCheck::Reselect);
	CHECK(repair.device.id == kHeadset.id);
	CHECK(repair.device.name == kHeadset.name);
}

TEST_CASE("Monitoring device: gone or ambiguous is left alone")
{
	const AudioDevice stale{kHeadset.name, "{old-id}"};
	CHECK(CheckMonitoringDevice(stale, {kSpeakers}).check == MonitoringCheck::Missing);
	CHECK(CheckMonitoringDevice({"", "{old-id}"}, {kSpeakers}).check == MonitoringCheck::Missing);
	const AudioDevice twin{kHeadset.name, "{twin}"};
	CHECK(CheckMonitoringDevice(stale, {kHeadset, twin}).check == MonitoringCheck::Ambiguous);
}

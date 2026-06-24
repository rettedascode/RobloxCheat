#include "engine/engine.h"

namespace sdk {

	std::uint64_t sdk::actor::userid() const {

		return drive->read<std::uint64_t>(this->Address + offsets::UserId);
	}

	std::uint64_t sdk::actor::teamid() const {

		return drive->read<uint64_t>(this->Address + offsets::Team);
	}

	sdk::actor sdk::actor::local() const {

		std::uint64_t local_address = drive->read<std::uint64_t>(this->Address + offsets::LocalPlayer);
		return sdk::actor(local_address);
	}

	std::string sdk::actor::display() const {

		return drive->readstring(this->Address + offsets::DisplayName);
	}
}

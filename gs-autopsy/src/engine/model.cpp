#include "engine/engine.h"

namespace sdk {

	std::uint64_t sdk::model::place() const {

		return drive->read<uint64_t>(this->Address + offsets::PlaceId);
	}

	std::uint64_t sdk::model::game() const {

		return drive->read<uint64_t>(this->Address + offsets::GameId);
	}

	std::uint64_t sdk::model::creator() const {

		return drive->read<uint64_t>(this->Address + offsets::CreatorId);
	}

	std::uint64_t sdk::model::server() const {

		return drive->read<uint64_t>(this->Address + offsets::ServerIP);
	}
}

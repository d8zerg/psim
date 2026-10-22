#include <google/protobuf/util/json_util.h>
#include <psim/response/v1/plan.pb.h>

#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "model.hpp"

int main() {
  try {
    const std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    if (input.size() > 1048576) {
      return 1;
    }
    const auto document = psim::response_engine::Json::parse(input);
    psim::response::v1::ResponsePlan plan;
    if (!google::protobuf::util::JsonStringToMessage(document.at("plan").dump(), &plan).ok()) {
      return 1;
    }
    std::cout << psim::response_engine::replay(plan, document.at("history")).dump() << '\n';
    return 0;
  } catch (const std::exception&) {
    std::cerr << "invalid replay input\n";
    return 1;
  }
}

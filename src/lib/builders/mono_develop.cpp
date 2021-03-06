#include "mono_develop.hpp"

#include <bacs/system/file.hpp>
#include <bacs/system/process.hpp>

#include <bunsan/static_initializer.hpp>

#include <boost/assert.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include <boost/regex.hpp>

namespace bacs {
namespace system {
namespace builders {

namespace {
struct invalid_lang_error : virtual invalid_argument_error {};
}  // namespace

BUNSAN_STATIC_INITIALIZER(bacs_system_builders_mono_develop, {
  BUNSAN_FACTORY_REGISTER_TOKEN(
      builder, mono_develop, [](const std::vector<std::string> &arguments) {
        return builder::make_shared<mono_develop>(arguments);
      })
})

static const boost::regex positional("[^=]+"), key_value("([^=]+)=(.*)");

mono_develop::mono_develop(const std::vector<std::string> &arguments)
    : m_configuration("Release") {
  for (const std::string &arg : arguments) {
    boost::smatch match;
    if (boost::regex_match(arg, match, key_value)) {
      BOOST_ASSERT(match.size() == 3);
      const std::string key = match[1].str(), value = match[2].str();
      if (key == "configuration") {
        m_configuration = value;
      } else {
        BOOST_THROW_EXCEPTION(invalid_argument_error()
                              << invalid_argument_error::argument(arg));
      }
    } else {
      BOOST_THROW_EXCEPTION(invalid_argument_error()
                            << invalid_argument_error::argument(arg));
    }
  }
}

static const std::string sln_builder = "bacs_system_build_mono_develop";

static boost::filesystem::path executable_path = "executable";

executable_ptr mono_develop::build_extracted(
    const ContainerPointer &container, const unistd::access::Id &owner_id,
    bunsan::tempfile &&tmpdir, const boost::filesystem::path &source,
    const bacs::process::ResourceLimits &resource_limits,
    bacs::process::BuildResult &result) {
  using boost::filesystem::recursive_directory_iterator;

  const boost::filesystem::path root =
      container->filesystem().containerPath(tmpdir.path());

  bool found = false;
  boost::filesystem::path solution;
  const boost::filesystem::path abs_source =
      container->filesystem().keepInRoot(source);
  for (recursive_directory_iterator i(abs_source), end; i != end; ++i) {
    if (boost::filesystem::is_regular_file(*i) &&
        i->path().extension() == ".sln") {
      if (found) {
        result.set_status(bacs::process::BuildResult::FAILED);
        result.set_output(str(boost::format(
                                  "Multiple solutions found: "
                                  "%1% and %2%") %
                              solution % i->path()));
        return nullptr;
      } else {
        found = true;
        solution = i->path();
      }
    }
  }
  if (!found) {
    result.set_status(bacs::process::BuildResult::FAILED);
    result.set_output("Solution (*.sln) not found");
    return nullptr;
  }
  solution = container->filesystem().containerPath(solution);

  const boost::filesystem::path executable = root / executable_path;
  const ProcessGroupPointer process_group = container->createProcessGroup();
  const ProcessPointer process = process_group->createProcess(sln_builder);
  process->setArguments(process->executable(), "--configuration",
                        m_configuration, "--output", executable, solution);
  process::setup(process_group, process, resource_limits);
  process->setCurrentPath(root);
  process->setOwnerId(owner_id);
  process->setStream(2, FdAlias(1));
  process->setStream(1, File("log", AccessMode::WRITE_ONLY));
  const ProcessGroup::Result process_group_result =
      process_group->synchronizedCall();
  const Process::Result process_result = process->result();
  const bool success = process::parse_result(
      process_group_result, process_result, *result.mutable_execution());
  result.set_output(file::read_first(tmpdir.path() / "log", 65536));
  if (success) {
    result.set_status(bacs::process::BuildResult::OK);
    return std::make_shared<project_executable>(container, std::move(tmpdir),
                                                executable_path);
  } else {
    result.set_status(bacs::process::BuildResult::FAILED);
    return nullptr;
  }
}

}  // namespace builders
}  // namespace system
}  // namespace bacs

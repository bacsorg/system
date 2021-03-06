#include "project.hpp"

#include <bacs/system/process.hpp>

#include <yandex/contest/system/unistd/Operations.hpp>

#include <bunsan/filesystem/fstream.hpp>
#include <bunsan/utility/archiver.hpp>
#include <bunsan/utility/system_resolver.hpp>

#include <boost/assert.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/scope_exit.hpp>

namespace bacs {
namespace system {
namespace builders {

static const boost::filesystem::path project_path = "/builders_project";

static const boost::filesystem::path source_path = "source";
static const boost::filesystem::path source_archive_path = "source_archive";

using boost::filesystem::recursive_directory_iterator;

static bool extract(const bacs::process::Source &source,
                    const boost::filesystem::path &root, std::string &error) {
  using namespace bunsan::utility;

  BOOST_ASSERT(source.has_archiver());

  boost::property_tree::ptree config;
  if (!source.archiver().format().empty())
    config.put("format", source.archiver().format());
  system_resolver rs;
  const archiver_ptr ar = archiver::instance(source.archiver().type(), rs);
  ar->setup(config);

  try {
    bunsan::filesystem::ofstream fout(root / source_archive_path);
    BUNSAN_FILESYSTEM_FSTREAM_WRAP_BEGIN(fout) {
      fout << source.data();
    } BUNSAN_FILESYSTEM_FSTREAM_WRAP_END(fout)
    fout.close();

    ar->unpack(root / source_archive_path, root / source_path);
  } catch (std::exception &e) {
    error = e.what();
    return false;
  }
  return true;
}

static void pre_build(const unistd::access::Id &owner_id,
                      const boost::filesystem::path &root) {
  for (recursive_directory_iterator i(root), end; i != end; ++i) {
    unistd::lchown(i->path(), owner_id);
  }
}

static void post_build(const unistd::access::Id & /*owner_id*/,
                       const boost::filesystem::path &root) {
  for (recursive_directory_iterator i(root), end; i != end; ++i) {
    unistd::lchown(i->path(), unistd::access::Id{0, 0});
    boost::filesystem::permissions(*i, boost::filesystem::remove_perms |
                                           boost::filesystem::owner_write |
                                           boost::filesystem::group_write |
                                           boost::filesystem::others_write);
    boost::filesystem::permissions(
        *i, boost::filesystem::add_perms | boost::filesystem::owner_read |
                boost::filesystem::group_read | boost::filesystem::others_read);
    if (i->status().permissions() & boost::filesystem::owner_exe)
      boost::filesystem::permissions(
          *i, boost::filesystem::add_perms | boost::filesystem::owner_exe |
                  boost::filesystem::group_exe | boost::filesystem::others_exe);
  }
}

executable_ptr project::build(
    const ContainerPointer &container, const unistd::access::Id &owner_id,
    const bacs::process::Source &source,
    const bacs::process::ResourceLimits &resource_limits,
    bacs::process::BuildResult &result) {
  try {
    if (!source.has_archiver()) {
      result.set_status(bacs::process::BuildResult::FAILED);
      result.set_output("Directory tree source is required");
      return nullptr;
    }
    const boost::filesystem::path solutions =
        container->filesystem().keepInRoot(project_path);
    boost::filesystem::create_directories(solutions);
    bunsan::tempfile tmpdir =
        bunsan::tempfile::directory_in_directory(solutions);
    container->filesystem().setOwnerId(project_path / tmpdir.path().filename(),
                                       owner_id);
    BOOST_VERIFY(
        boost::filesystem::create_directory(tmpdir.path() / source_path));

    std::string error;
    if (!extract(source, tmpdir.path(), error)) {
      result.set_status(bacs::process::BuildResult::FAILED);
      result.set_output(error);
      return nullptr;
    }

    BOOST_VERIFY(boost::filesystem::create_directory(
        container->filesystem().keepInRoot("/tmp")));
    container->filesystem().setMode("/tmp", 01777);
    BOOST_SCOPE_EXIT_ALL(&) {
      boost::filesystem::remove_all(container->filesystem().keepInRoot("/tmp"));
    };

    const boost::filesystem::path root = tmpdir.path();
    pre_build(owner_id, root);
    const executable_ptr exe = build_extracted(
        container, owner_id, std::move(tmpdir),
        project_path / root.filename() / source_path, resource_limits, result);
    if (exe) post_build(owner_id, root);
    return exe;
  } catch (std::exception &) {
    BOOST_THROW_EXCEPTION(builder_build_error()
                          << bunsan::enable_nested_current());
  }
}

project_executable::project_executable(
    const ContainerPointer &container, bunsan::tempfile &&tmpdir,
    const boost::filesystem::path &executable)
    : m_container(container),
      m_tmpdir(std::move(tmpdir)),
      m_executable(executable) {}

ProcessPointer project_executable::create(
    const ProcessGroupPointer &process_group,
    const ProcessArguments &arguments) {
  const ProcessPointer process = process_group->createProcess(executable());
  process->setArguments(process->executable(), arguments);
  return process;
}

ContainerPointer project_executable::container() { return m_container; }

boost::filesystem::path project_executable::dir() const {
  return project_path / m_tmpdir.path().filename();
}

boost::filesystem::path project_executable::source() const {
  return dir() / source_path;
}

boost::filesystem::path project_executable::executable() const {
  return dir() / m_executable;
}

}  // namespace builders
}  // namespace system
}  // namespace bacs

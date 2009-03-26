/*---------------------------------------------------------------------------*\
                          ____  _ _ __ _ __  ___ _ _
                         |_ / || | '_ \ '_ \/ -_) '_|
                         /__|\_, | .__/ .__/\___|_|
                             |__/|_|  |_|
\*---------------------------------------------------------------------------*/

#include <iostream>
#include <sstream>
#include <boost/format.hpp>

#include "zypp/ZYppFactory.h"
#include "zypp/base/Logger.h"
#include "zypp/FileChecker.h"

#include "zypp/Edition.h"
#include "zypp/Patch.h"
#include "zypp/Package.h"

#include "zypp/media/MediaException.h"

#include "misc.h"
#include "utils/misc.h"
#include "utils/getopt.h"
#include "utils/prompt.h"
#include "Summary.h"

#include "solve-commit.h"

using namespace std;
using namespace zypp;
using namespace boost;

extern ZYpp::Ptr God;


//! @return true to retry solving now, false to cancel, indeterminate to continue
static TriBool show_problem (Zypper & zypper,
                             const ResolverProblem & prob,
                             ProblemSolutionList & todo)
{
  ostringstream desc_stm;
  string tmp;
  // translators: meaning 'dependency problem' found during solving
  desc_stm << _("Problem: ") << prob.description () << endl;
  tmp = prob.details ();
  if (!tmp.empty ())
    desc_stm << "  " << tmp << endl;

  int n;
  ProblemSolutionList solutions = prob.solutions ();
  ProblemSolutionList::iterator
    bb = solutions.begin (),
    ee = solutions.end (),
    ii;
  for (n = 1, ii = bb; ii != ee; ++n, ++ii) {
    // TranslatorExplanation %d is the solution number
    desc_stm << format (_(" Solution %d: ")) % n << (*ii)->description () << endl;
    tmp = (*ii)->details ();
    if (!tmp.empty ())
      desc_stm << indent(tmp, 2) << endl;
  }

  unsigned int problem_count = God->resolver()->problems().size();
  unsigned int solution_count = solutions.size();

  // without solutions, its useless to prompt
  if (solutions.empty())
  {
    zypper.out().error(desc_stm.str());
    return false;
  }

  string prompt_text;
  if (problem_count > 1)
    prompt_text = _PL(
      "Choose the above solution using '1' or skip, retry or cancel",
      "Choose from above solutions by number or skip, retry or cancel",
      solution_count);
  else
    prompt_text = _PL(
      // translators: translate 'c' to whatever you translated the 'c' in
      // "c" and "s/r/c" strings
      "Choose the above solution using '1' or cancel using 'c'",
      "Choose from above solutions by number or cancel",
      solution_count);

  PromptOptions popts;
  unsigned int default_reply;
  ostringstream numbers;
  for (unsigned int i = 1; i <= solution_count; i++)
    numbers << i << "/";

  if (problem_count > 1)
  {
    default_reply = solution_count + 2;
    // translators: answers for dependency problem solution input prompt:
    // "Choose from above solutions by number or skip, retry or cancel"
    // Translate the letters to whatever is suitable for your language.
    // The anserws must be separated by slash characters '/' and must
    // correspond to skip/retry/cancel in that order.
    // The answers should be lower case letters.
    popts.setOptions(numbers.str() + _("s/r/c"), default_reply);
  }
  else
  {
    default_reply = solution_count;
    // translators: answers for dependency problem solution input prompt:
    // "Choose from above solutions by number or cancel"
    // Translate the letter 'c' to whatever is suitable for your language
    // and to the same as you translated it in the "s/r/c" string
    // See the "s/r/c" comment for other details.
    // One letter string  for translation can be tricky, so in case of problems,
    // please report a bug against zypper at bugzilla.novell.com, we'll try to solve it.
    popts.setOptions(numbers.str() + _("c"), default_reply);
  }

  zypper.out().prompt(PROMPT_DEP_RESOLVE, prompt_text, popts, desc_stm.str());
  unsigned int reply =
    get_prompt_reply(zypper, PROMPT_DEP_RESOLVE, popts);

  // retry
  if (problem_count > 1 && reply == solution_count + 1)
    return true;
  // cancel (one problem)
  if (problem_count == 1 && reply == solution_count)
    return false;
  // cancel (more problems)
  if (problem_count > 1 && reply == solution_count + 2)
    return false;
  // skip
  if (problem_count > 1 && reply == solution_count)
    return indeterminate; // continue with next problem

  zypper.out().info(boost::str(format (_("Applying solution %s")) % (reply + 1)), Out::HIGH);
  ProblemSolutionList::iterator reply_i = solutions.begin ();
  advance (reply_i, reply);
  todo.push_back (*reply_i);

  tribool go_on = indeterminate; // continue with next problem
  return go_on;
}

// return true to retry solving, false to cancel transaction
static bool show_problems(Zypper & zypper)
{
  bool retry = true;
  Resolver_Ptr resolver = zypp::getZYpp()->resolver();
  ResolverProblemList rproblems = resolver->problems ();
  ResolverProblemList::iterator
    b = rproblems.begin (),
    e = rproblems.end (),
    i;
  ProblemSolutionList todo;

  // display the number of problems
  if (rproblems.size() > 1)
    zypper.out().info(boost::str(format(
      _PL("%d Problem:", "%d Problems:", rproblems.size())) % rproblems.size()));
  else if (rproblems.empty())
  {
    // should not happen! If solve() failed at least one problem must be set!
    zypper.out().error(_("Specified capability not found"));
    zypper.setExitCode(ZYPPER_EXIT_INF_CAP_NOT_FOUND);
    return false;
  }

  // for many problems, list them shortly first
  //! \todo handle resolver problems caused by --capability mode arguments specially to give proper output (bnc #337007)
  if (rproblems.size() > 1)
  {
    for (i = b; i != e; ++i)
      zypper.out().info(boost::str(
        format(_("Problem: %s")) % (*i)->description()));
  }
  // now list all problems with solution proposals
  for (i = b; i != e; ++i)
  {
    zypper.out().info("", Out::NORMAL, Out::TYPE_NORMAL); // visual separator
    TriBool stopnow = show_problem(zypper, *(*i), todo);
    if (! indeterminate (stopnow)) {
      retry = stopnow == true;
      break;
    }
  }

  if (retry)
  {
    zypper.out().info(_("Resolving dependencies..."));
    resolver->applySolutions (todo);
  }
  return retry;
}

static void dump_pool ()
{
  int count = 1;
  static bool full_pool_shown = false;

  _XDEBUG( "---------------------------------------" );
  for (ResPool::const_iterator it =   God->pool().begin(); it != God->pool().end(); ++it, ++count) {

    if (!full_pool_shown                                    // show item if not shown all before
        || it->status().transacts()                         // or transacts
        || !it->isBroken())                                 // or broken status
    {
      _XDEBUG( count << ": " << *it );
    }
  }
  _XDEBUG( "---------------------------------------" );
  full_pool_shown = true;
}


static void set_force_resolution(Zypper & zypper)
{
  // don't force resolution in 'verify'
  if (zypper.command() == ZypperCommand::VERIFY)
  {
    God->resolver()->setForceResolve(false);
    return;
  }

  // --force-resolution command line parameter value
  TriBool force_resolution = zypper.runtimeData().force_resolution;

  if (zypper.cOpts().count("force-resolution"))
    force_resolution = true;
  if (zypper.cOpts().count("no-force-resolution"))
  {
    if (force_resolution)
      zypper.out().warning(str::form(
        // translators: meaning --force-resolution and --no-force-resolution
        _("%s conflicts with %s, will use the less aggressive %s"),
          "--force-resolution", "--no-force-resolution", "--no-force-resolution"));
    force_resolution = false;
  }

  // if --force-resolution was not specified on the command line, force
  // the resolution by default for the install and remove commands and the
  // rug_compatible mode. Don't force resolution in non-interactive mode
  // and for update and dist-upgrade command (complex solver request).
  // bnc #369980
  if (indeterminate(force_resolution))
  {
    if (!zypper.globalOpts().non_interactive &&
        (zypper.globalOpts().is_rug_compatible ||
         zypper.command() == ZypperCommand::INSTALL ||
         zypper.command() == ZypperCommand::REMOVE))
      force_resolution = true;
    else
      force_resolution = false;
  }

  // save the setting
  zypper.runtimeData().force_resolution = force_resolution;

  DBG << "force resolution: " << force_resolution << endl;
  ostringstream s;
  s << _("Force resolution:") << " " << (force_resolution ? _("Yes") : _("No"));
  zypper.out().info(s.str(), Out::HIGH);

  God->resolver()->setForceResolve(force_resolution);
}

static void set_no_recommends(Zypper & zypper)
{
  bool no_recommends = false;
  if (zypper.command() == ZypperCommand::REMOVE)
    // never install recommends when removing packages
    no_recommends = true;
  else
    // install also recommended packages unless --no-recommends is specified
    no_recommends = zypper.cOpts().count("no-recommends");
  DBG << "no recommends (only requires): " << no_recommends << endl;
  God->resolver()->setOnlyRequires(no_recommends);
}


static void set_ignore_recommends_of_installed(Zypper & zypper)
{
  bool ignore = true;
  if (zypper.command() == ZypperCommand::DIST_UPGRADE ||
      zypper.command() == ZypperCommand::INSTALL_NEW_RECOMMENDS)
    ignore = false;
  DBG << "ignore recommends of already installed packages: " << ignore << endl;
  God->resolver()->setIgnoreAlreadyRecommended(ignore);
}


static void set_solver_flags(Zypper & zypper)
{
  set_force_resolution(zypper);
  set_no_recommends(zypper);
  set_ignore_recommends_of_installed(zypper);
}


/**
 * Run the solver.
 *
 * \return <tt>true</tt> if a solution has been found, <tt>false</tt> otherwise
 */
bool resolve(Zypper & zypper)
{
  dump_pool(); // debug
  set_solver_flags(zypper);
  DBG << "Calling the solver..." << endl;
  return God->resolver()->resolvePool();
}

static bool verify(Zypper & zypper)
{
  dump_pool();
  set_solver_flags(zypper);
  zypper.out().info(_("Verifying dependencies..."), Out::HIGH);
  DBG << "Calling the solver to verify system..." << endl;
  return God->resolver()->verifySystem();
}

static bool dist_upgrade(Zypper & zypper, zypp::UpgradeStatistics & dup_stats)
{
  dump_pool();
  set_solver_flags(zypper);
  zypper.out().info(_("Computing upgrade..."), Out::HIGH);
  DBG << "Calling the solver doUpgrade()..." << endl;
  return God->resolver()->doUpgrade(dup_stats);
}

/**
 * To be called after setting solver flags and calling solver methods
 * (like doUpdate(), doUpgrade(), verify(), and resolve()) to generate
 * solver testcase.
 */
static void make_solver_test_case(Zypper & zypper)
{
//  set_solver_flags(zypper);

  string testcase_dir("/var/log/zypper.solverTestCase");

  zypper.out().info(_("Generating solver test case..."));
  if (God->resolver()->createSolverTestcase(testcase_dir))
    zypper.out().info(boost::str(
      format(_("Solver test case generated successfully at %s."))
        % testcase_dir));
  else
  {
    zypper.out().error(_("Error creating the solver test case."));
    zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
  }
}

// ----------------------------------------------------------------------------
// commit
// ----------------------------------------------------------------------------

/**
 * Calls the appropriate solver function with flags according to current
 * command and options, show the summary, and commits.
 *
 * @return ZYPPER_EXIT_OK - successful commit,
 *  ZYPPER_EXIT_ERR_ZYPP - if ZYppCommitResult contains resolvables with errors,
 *  ZYPPER_EXIT_INF_REBOOT_NEEDED - if one of patches to be installed needs machine reboot,
 *  ZYPPER_EXIT_INF_RESTART_NEEDED - if one of patches to be installed needs package manager restart
 */
void solve_and_commit (Zypper & zypper)
{
  bool show_forced_problems = true;
  bool commit_done = false;
  do
  {
    // CALL SOLVER

    // e.g. doUpdate unsets this flag, no need for another solving
    if (zypper.runtimeData().solve_before_commit)
    {
      MIL << "solving..." << endl;

      while (true)
      {
        bool success;
        if (zypper.command() == ZypperCommand::VERIFY)
          success = verify(zypper);
        else if (zypper.command() == ZypperCommand::DIST_UPGRADE)
        {
          zypp::UpgradeStatistics dup_stats;
          zypper.out().info(_("Computing distribution upgrade..."));
          success = dist_upgrade(zypper, dup_stats);
          //! \todo make use of the upgrade stats
        }
        else
        {
          zypper.out().info(_("Resolving package dependencies..."));
          success = resolve(zypper);
        }

        // go on, we've got solution or we don't want a solution (we want testcase)
        if (success || zypper.cOpts().count("debug-solver"))
          break;

        success = show_problems(zypper);
        if (!success)
        {
          zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP); // bnc #242736
          return;
        }
      }
    }

    if (zypper.cOpts().count("debug-solver"))
    {
      make_solver_test_case(zypper);
      return;
    }

    MIL << "got solution, showing summary" << endl;

    // SHOW SUMMARY

    Summary summary(God->pool());

    // if running on SUSE Linux Enterprise, report unsupported packages
    Product::constPtr platform = God->target()->baseProduct();
    if (platform && platform->name().find("SUSE_SLE") != string::npos)
      summary.setViewOption(Summary::SHOW_UNSUPPORTED);

    // show the summary
    if (zypper.out().type() == Out::TYPE_XML)
      summary.dumpAsXmlTo(cout);
    else
      summary.dumpTo(cout);


    if (summary.packagesToGetAndInstall() ||
        summary.packagesToRemove() ||
        !zypper.runtimeData().srcpkgs_to_install.empty())
    {
      if (zypper.command() == ZypperCommand::VERIFY)
        zypper.out().info(_("Some of the dependencies of installed packages are broken."
            " In order to fix these dependencies, the following actions need to be taken:"));

      // check root user
      if (zypper.command() == ZypperCommand::VERIFY && geteuid() != 0
        && !zypper.globalOpts().changedRoot)
      {
        zypper.out().error(
          _("Root privileges are required to fix broken package dependencies."));
        zypper.setExitCode(ZYPPER_EXIT_ERR_PRIVILEGES);
        return;
      }

      // PROMPT

      bool show_p_option =
        (summary.packagesToRemove() && (
          zypper.command() == ZypperCommand::INSTALL ||
          zypper.command() == ZypperCommand::UPDATE))
        ||
        (summary.packagesToGetAndInstall() &&
          zypper.command() == ZypperCommand::REMOVE);

      bool do_commit = false;
      if (zypper.runtimeData().force_resolution && show_p_option)
      {
        PromptOptions popts;
        // translators: Yes / No / show Problems. This prompt will appear
        // after install/update command summary if there will be any package
        // to-be-removed automatically to show why, if asked.
        // Translate to whathever is suitable for your language
        // The anserws must be separated by slash characters '/' and must
        // correspond to yes/no/showproblems in that order.
        // The answers should be lower case letters.
        popts.setOptions(_("y/n/p"), 0);
        // translators: help text for 'y' option in the y/n/p prompt
        popts.setOptionHelp(0, _("Accept the summary and proceed with installation/removal of packages."));
        // translators: help text for 'n' option in the y/n/p prompt
        popts.setOptionHelp(1, _("Cancel the operation."));
        // translators: help text for 'p' option in the y/n/p prompt
        popts.setOptionHelp(2, _("Restart solver in no-force-resolution mode in order to show dependency problems."));
        string prompt_text = _("Continue?");
        zypper.out().prompt(PROMPT_YN_INST_REMOVE_CONTINUE, prompt_text, popts);
        unsigned int reply =
          get_prompt_reply(zypper, PROMPT_YN_INST_REMOVE_CONTINUE, popts);

        if (reply == 2)
        {
          // one more solver solver run with force-resoltion off
          zypper.runtimeData().force_resolution = false;
          // undo solver changes before retrying
          God->resolver()->undo();
          continue;
        }
        else if (reply == 1)
          show_forced_problems = false;
        else
        {
          do_commit = true;
          show_forced_problems = false;
        }
      }
      // no dependency problems
      else
      {
        do_commit = read_bool_answer(PROMPT_YN_INST_REMOVE_CONTINUE, _("Continue?"), true);
        show_forced_problems = false;
      }

      // COMMIT

      if (do_commit)
      {
        if (!confirm_licenses(zypper))
          return;

        {
          try
          {
            RuntimeData & gData = Zypper::instance()->runtimeData();
            gData.show_media_progress_hack = true;
            // Total packages to download & install.
            // To be used to write overall progress.
            gData.commit_pkgs_total = summary.packagesToGetAndInstall();
            gData.commit_pkg_current = 0;

            ostringstream s;
            s << _("committing"); MIL << "committing...";

            ZYppCommitResult result;
            if (copts.count("dry-run"))
            {
              s << " " << _("(dry run)") << endl; MIL << "(dry run)";
              zypper.out().info(s.str(), Out::HIGH);

              result = God->commit(ZYppCommitPolicy().dryRun(true));
            }
            else
            {
              zypper.out().info(s.str(), Out::HIGH);

              result = God->commit(
                ZYppCommitPolicy().syncPoolAfterCommit(zypper.runningShell()));

              commit_done = true;
            }


            MIL << endl << "DONE" << endl;

            gData.show_media_progress_hack = false;

            if (!result._errors.empty())
              zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);

            s.clear(); s << result;
            zypper.out().info(s.str(), Out::HIGH);
          }
          catch ( const media::MediaException & e ) {
            ZYPP_CAUGHT(e);
            zypper.out().error(e,
                _("Problem retrieving the package file from the repository:"),
                _("Please see the above error message for a hint."));
            zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
            return;
          }
          catch ( zypp::repo::RepoException & e ) {
            ZYPP_CAUGHT(e);

            RepoManager manager(zypper.globalOpts().rm_options );

            bool refresh_needed = false;
            try
            {
              for(RepoInfo::urls_const_iterator it = e.info().baseUrlsBegin();
                        it != e.info().baseUrlsEnd(); ++it)
                {
                  RepoManager::RefreshCheckStatus stat = manager.
                                checkIfToRefreshMetadata(e.info(), *it,
                                RepoManager::RefreshForced );
                  if ( stat == RepoManager::REFRESH_NEEDED )
                  {
                    refresh_needed = true;
                    break;
                  }
                }
            }
            catch (const Exception &)
            { DBG << "check if to refresh exception caught, ignoring" << endl; }

            std::string hint = _("Please see the above error message for a hint.");
            if (refresh_needed)
            {
              hint = boost::str(format(
                  // translators: the first %s is 'zypper refresh' and the second
                  // is repo allias
                  _("Repository '%s' is out of date. Running '%s' might help.")) %
                  e.info().alias() % "zypper refresh" );
            }
            zypper.out().error(e,
                _("Problem retrieving the package file from the repository:"),
                hint);
            zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
            return;
          }
          catch ( const zypp::FileCheckException & e ) {
            ZYPP_CAUGHT(e);
            zypper.out().error(e,
                _("The package integrity check failed. This may be a problem"
                " with the repository or media. Try one of the following:\n"
                "\n"
                "- just retry previous command\n"
                "- refresh the repositories using 'zypper refresh'\n"
                "- use another installation medium (if e.g. damaged)\n"
                "- use another repository"));
            zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
            return;
          }
          catch ( const Exception & e ) {
            ZYPP_CAUGHT(e);
            zypper.out().error(e,
                _("Problem occured during or after installation or removal of packages:"),
                _("Please see the above error message for a hint."));
            zypper.setExitCode(ZYPPER_EXIT_ERR_ZYPP);
          }
        }

        // install any pending source packages
        //! \todo This won't be necessary once we get a new solver flag
        //! for installing source packages without their build deps
        if (!zypper.runtimeData().srcpkgs_to_install.empty())
          install_src_pkgs(zypper);

        // set return value to 'reboot needed'
        if (summary.needMachineReboot())
        {
          zypper.setExitCode(ZYPPER_EXIT_INF_REBOOT_NEEDED);
          zypper.out().warning(
            _("One of installed patches requires reboot of"
              " your machine. Reboot as soon as possible."), Out::QUIET);
        }
        // set return value to 'restart needed' (restart of package manager)
        // however, 'reboot needed' takes precedence
        else if (zypper.exitCode() != ZYPPER_EXIT_INF_REBOOT_NEEDED && summary.needPkgMgrRestart())
        {
          zypper.setExitCode(ZYPPER_EXIT_INF_RESTART_NEEDED);
          zypper.out().warning(
            _("One of installed patches affects the package"
              " manager itself. Run this command once more to install any other"
              " needed patches."),
            Out::QUIET, Out::TYPE_NORMAL); // don't show this to machines
        }
      }
    }
    // noting to do
    else
    {
      if (zypper.command() == ZypperCommand::VERIFY)
        zypper.out().info(_("Dependencies of all installed packages are satisfied."));
      else
        zypper.out().info(_("Nothing to do."));

      break;
    }
  }
  while (show_forced_problems);
}

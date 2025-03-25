#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

r"""
### What this script does
This script looks at the fix commits between two versions of Blender
and tries to figure out if the commit fixed a issue that has been in
Blender for one or more major releases, or if the commit fixed a issue
introduced in this release.

This is done so we can create a "bug fixes" page in our release notes
listing commits that fix old issues.

The list generated by this script isn't 100% accurate (there will be
some missing commits), but it's significantly better than nothing.

---

### How to use the script
- Make sure the list `LIST_OF_OFFICIAL_BLENDER_VERSIONS` is up to date.
- Open a terminal in your Blender source code folder and make sure the
  branches you're interested in are up to date.
- Launch `main.py` with relevant launch arguments. The required arguments are:
  - --current-version (-cv)
  - --previous-version (-pv)
  - --current-release-tag (-ct)
  - --previous-release-tag (-pt)
  - --backport-tasks (-bpt) (Optional but highly recommended)
- Here is an example if you wish to collect the list for Blender 4.4 during
the Beta and onwards stage of development:
  - `python bug_fixes_per_major_release.py -cv 4.4 -pv 4.3 -ct blender-v4.4-release -pt v4.3.2 -bpt 109399 124452 130221`
- Here is an example if you wish to collect the list for Blender 4.5 during
the Alpha stage of development.
  - `python bug_fixes_per_major_release.py -cv 4.5 -pv 4.4 -ct main -pt blender-v4.4-release -bpt 109399 124452`
- Wait for the script to finish (This can take upwards of 20 minutes).
- Follow the guide printed to terminal.

#### Additional usage
Sometimes commits can't be automatically sorted by this script.
Specifically the fixed issue listed in the commit message may be
incorrect.

In situations like this it can be easier to simply override the issue that
the commit claims to fix. This can be done by launching the script with:
`bug_fixes_per_major_release.py -o`

The script will then ask for the commit hash, then the
issue number that commit actually fixes then will use that override
(and all other overrides you've setup) when you run the script again.

---

### How the script works
- First the script gathers all commits that contain `Fix #NUMBER` that
occured between the two versions of Blender you're interested in.
  - This is done using:
  `git --no-pager log PREVIOUS_VERSION..CURRENT_VERSION --oneline -i -P --grep "Fix.*#+\d+"`
- The script then extracts all report numbers (`#NUMBER`)
  from the commit message.
- The script then iterates through those reports, checking the
  "Broken" and "Working" fields to try and figure out whether
  the commit fixed a issue that existed in earlier versions of Blender
  or not.
- Finally the list of commits and further instructions is printed to the
  terminal either for review or to be posted in the release notes.

---

### Limitations
- Revert commits are not handled automatically
  - Revert commits can either fix a bug, or revert a commit that fixes
  a bug. Distinguishing between the two can be difficult, so the script
  doesn't process these commits and instead leaves it to be manually
  sorted by the person creating the release notes.
- Fix commits that do not contain `Fix #NUMBER` in their commit message
are not included in the output of this script.
  - This is because the script figures out if a commit fixed a old
  issue or not based on the information in the bug report it fixed.
  If a bug report isn't listed in the commit message, then the script
  can't sort it and so it's ignored.
- If a user puts weird version numbers in their "Broken" or "Working"
fields on their bug report, then this script could accidentally sorted
the associated fix commit into the wrong category.
  - This is a consequence of the simple nature of the system the script
  uses to extract Blender versions from a bug report.
  - The triaging team will do their best to remove problematic
  version numbers from reports when they first encounter them.
- The script does not know about long term projects. So some fix
commits that obviously shouldn't go into the release notes can end up
in the output.
  - Take for example Grease Pencil v3 (GPv3). GPv3 was in development
  for multiple versions of Blender. As a result there are bug reports
  from 4.2 Alpha and 4.3 (the version it officially released in).
  If a fix commit in 4.3 claims to fix one of the reports made in 4.2,
  then that fix commit will show up in the list of "commits that fixed
  old issues" list. Which is incorrect as GPv3 was only released in 4.3.
  - These commits will need to be manually removed before posting the
  list to the release notes.

### Filling out bug report forms with relevant information
This script relies on bug reports having enough information in the
"Broken" and "Working" fields to be sorted. If a bug report does not
have enough information, it can not be sorted automatically and will
need to be manually sorted. The recommended process for manually
sorting these commits is to go to the relevant report that was fixed,
and update the "Broken" and "Working" fields with relevant information.

Figuring out how much information you need to put on a report for this
script can be confusing. So this section will try to clarify the
information required.

The script is looking for `Major.Minor` Blender versions on lines
that start with either "Broken" or "Working". So all the important
information should go on these lines. So if a report says:
"It worked in 4.3", then it needs to be rephrased to "Worked: 4.3"
for the script to find it.

The amount and type of information you put on each report will vary.
Here is a general outline of the order in which the script checks these
fields and this can help guide how much information needs to go on the
report.

From now on I will assume you are using this script to get the release
notes for Blender 4.4.

- The script looks at all "Broken" versions of Blender. If at least
one of them is below the current release, then the commit can be sorted.
  - E.g. `Broken: 4.4, 4.3` will allow the commit to be sorted as `4.3`
  is below the current release.
  - So if you know the bug is also present in a older version of
  Blender, then add that version to the list of broken versions.
- If the script can't sort based on the Broken field, it will try and
sort based on combined information from the Broken and Working fields.
  - E.g. `Broken: 4.4`. At this current point in time the script knows
  the issue is there in 4.4, but doesn't know if the issue was
  introduced in 4.4, or has been around for longer and only reported in
  4.4.
  - So the script then looks at the "Working" field. And if the working
  version is the current release, or the previous release, then we
  assume the issue was introduced in this release and can sort the
  report. For example `Worked: 4.3` or `Worked: 4.4` will allow the
  script to sort this report. But `Worked: 4.2` will not.
  - So if you test the previous version and find it works, then add
  it to the working field. Or if you bisect 4.4 to find the cause then
  add something like `Worked: Prior to 4.4 COMMIT_HASH`

Along with those guide lines above, it can be confusing about what to
do for bug reports for features introduced in the current release.
The broken version is 4.4, so the script can't sort based on that, but
there is no working version.

My recommendation is to include the current or previous version in the
working field with a comment explaining it. For example:
`Worked: Never, as this feature was introduced in 4.4`
Or
`Worked: 4.3 as it did not have this feature`
"""

__all__ = (
    "main",
)

import re
import sys
import json
import subprocess
import argparse
import urllib.error
import urllib.request

from time import time, sleep
from typing import Any
from pathlib import Path


# -----------------------------------------------------------------------------
# Constants used throughout the script

UNKNOWN = "UNKNOWN"

FIXED_NEW_ISSUE = "FIXED NEW"
NEEDS_MANUAL_SORTING = "MANUALLY SORT"
FIXED_OLD_ISSUE = "FIXED OLD"
FIXED_PR = "FIXED PR"
REVERT = "REVERT"
IGNORED = "IGNORED"

SORTED_CLASSIFICATIONS = [FIXED_NEW_ISSUE, FIXED_OLD_ISSUE, IGNORED]
VALID_CLASSIFICATIONS = [FIXED_NEW_ISSUE, NEEDS_MANUAL_SORTING, FIXED_OLD_ISSUE, FIXED_PR, REVERT, IGNORED]

OLDER_VERION = "OLDER"
NEWER_VERION = "NEWER"
SAME_VERION = "SAME"

dir_of_script = Path(__file__).parent.resolve()
PATH_TO_OVERRIDES = dir_of_script.joinpath('overrides.json')
PATH_TO_CACHED_COMMITS = dir_of_script.joinpath('cached_commits.json')
del dir_of_script

# Add recent Blender versions to this list, including in-development versions.
# This list is used to identify if a version number found in a report is a valid version number.
# This is to help eliminate dates and other weird information people put
# in their reports in the format of a version number.

LIST_OF_OFFICIAL_BLENDER_VERSIONS = (
    # 1.x.
    '1.0', '1.60', '1.73', '1.80',
    # 2.0X.
    '2.04',
    # 2.2x.
    '2.26', '2.27', '2.28',
    # 2.3x.
    '2.30', '2.31', '2.32', '2.33', '2.34', '2.35', '2.36', '2.37', '2.39',
    # 2.4x.
    '2.40', '2.41', '2.42', '2.43', '2.44', '2.45', '2.46', '2.47', '2.48', '2.49',
    # 2.5x.
    '2.50', '2.53', '2.54', '2.55', '2.56', '2.57', '2.58', '2.59',
    # 2.6x.
    '2.60', '2.61', '2.62', '2.63', '2.64', '2.65', '2.66', '2.67', '2.68', '2.69',
    # 2.7x.
    '2.70', '2.71', '2.72', '2.73', '2.74', '2.75', '2.76', '2.77', '2.78', '2.79',
    # 2.8x.
    '2.80', '2.81', '2.82', '2.83',
    # 2.9x.
    '2.90', '2.91', '2.92', '2.93',
    # 3.x.
    '3.0', '3.1', '3.2', '3.3', '3.4', '3.5', '3.6', '4.0',
    # 4.x.
    '4.1', '4.2', '4.3', '4.4', '4.5'
)

# Catch duplicates
assert len(set(LIST_OF_OFFICIAL_BLENDER_VERSIONS)) == len(LIST_OF_OFFICIAL_BLENDER_VERSIONS)


# -----------------------------------------------------------------------------
# Private Utilities

# Conform to Blenders crawl delay request:
# https://projects.blender.org/robots.txt
crawl_delay = 2
last_checked_time = None


def url_json_get(url: str) -> Any:
    global last_checked_time

    if last_checked_time is not None:
        sleep(max(crawl_delay - (time() - last_checked_time), 0))
    last_checked_time = time()

    try:
        # Make the HTTP request and store the response in a 'response' object
        with urllib.request.urlopen(url) as response:
            result_bytes = response.read()
    except urllib.error.URLError as ex:
        print(url)
        print(f"Error making HTTP request: {ex}")
        return None

    # Convert the response content to a JSON object containing the user information.
    result = json.loads(result_bytes)
    assert result is None or isinstance(result, (dict, list))
    return result


# -----------------------------------------------------------------------------
# Commit Info Class

class CommitInfo:
    __slots__ = (
        "hash",
        "commit_title",

        "backport_list",
        "classification",
        "fixed_reports",
        "fixed_reports",
        "has_been_overwritten",
        "is_revert",
        "module",
        "needs_update",
        "report_title",
    )

    def __init__(self, commit_line: str) -> None:
        split_message = commit_line.split()

        # Commit line is in the format:
        # COMMIT_HASH Title of commit
        self.hash = split_message[0]
        self.commit_title = " ".join(split_message[1:])

        self.set_defaults()

    def set_defaults(self) -> None:
        self.is_revert = 'revert' in self.commit_title.lower()

        self.fixed_reports: list[str] = []
        self.check_full_commit_message_for_fixed_reports()

        # Setup some "useful" empty defaults.
        self.backport_list: list[str] = []
        self.module = UNKNOWN
        self.report_title = UNKNOWN
        self.classification = UNKNOWN

        # Variables below this point should not be saved to the cache.
        self.needs_update = True
        self.has_been_overwritten = False

    def check_full_commit_message_for_fixed_reports(self) -> None:
        command = ['git', 'show', '-s', '--format=%B', self.hash]
        command_output = subprocess.run(command, capture_output=True).stdout.decode('utf-8')

        # Find every instance of #NUMBER. These are the report that the commit claims to fix.
        match = re.findall(r'#(\d+)', command_output)
        if match:
            self.fixed_reports = match

    def get_backports(self, dict_of_backports: dict[str, list[str]]) -> None:
        # Figures out if the commit was back-ported, and to what version(s).
        if self.needs_update:
            for version_number in dict_of_backports:
                for backported_commit in dict_of_backports[version_number]:
                    if self.hash.startswith(backported_commit):
                        self.backport_list.append(version_number)
                        break

        if len(self.backport_list) > 0:
            # If the fix was back-ported to a old release, then it fixed a old issue.
            self.classification = FIXED_OLD_ISSUE

    def override_report_info(self, new_classification: str, new_title: str, new_module: str) -> bool:
        if new_classification in SORTED_CLASSIFICATIONS:
            # Clear classifications are more important then any other. So always override in this case.
            self.classification = new_classification
            self.report_title = new_title
            self.module = new_module
            return True

        if new_classification in (NEEDS_MANUAL_SORTING, FIXED_PR):
            if (self.classification == UNKNOWN) or ((new_classification ==
                                                     NEEDS_MANUAL_SORTING) and (self.classification == FIXED_PR)):
                # Only replace information if the previous classification was the default (UNKNOWN)
                # or the new classification is NEEDS_MANUAL_SORTING and the old one was
                # FIXED_PR (NEEDS_MANUAL_SORTING is more useful than FIXED_PR).
                self.classification = new_classification
                self.report_title = new_title
                self.module = new_module

        return False

    def get_module(self, labels: list[dict[Any, Any]]) -> str:
        # Figures out what module the report that was fixed belongs too.
        for label in labels:
            if "module" in label['name'].lower():
                # Module labels are typically in the format Module/NAME.
                return " ".join(label['name'].split("/")[1:])

        return UNKNOWN

    def classify(
            self,
            *,
            current_version: str,
            previous_version: str,
    ) -> None:
        if not self.needs_update:
            # The data was loaded from cache, no need to reprocess it.
            return

        if self.is_revert:
            self.classification = REVERT
            # Give reverts commits their commit title so when it is printed to terminal, it has a useful name.
            self.report_title = self.commit_title
            return

        for report_number in self.fixed_reports:
            report_information = url_json_get(
                f"https://projects.blender.org/api/v1/repos/blender/blender/issues/{report_number}")

            report_title = report_information['title']
            module = self.get_module(report_information['labels'])

            if "pull" in report_information['html_url']:
                # The fixed issue turns out to be a pull request.
                # This was probably a typo, but note it down away so we can check and fix it.
                self.override_report_info(FIXED_PR, self.commit_title, UNKNOWN)
            else:
                classification = classify_based_on_report(
                    report_information['body'],
                    current_version=current_version,
                    previous_version=previous_version,
                )
                if self.override_report_info(classification, report_title, module):
                    # The commit has been sorted. No need to process more reports.
                    break

    def generate_release_note_ready_string(self) -> str:
        # Breakup report_title based on words, and remove `:` if it's at the end of the first word.
        # This is because the website the release notes are being posted to applies some undesirable
        # formatting to ` * Word:`.
        title = self.report_title
        split_title = title.split()
        split_title[0] = split_title[0].strip(":")

        # Capitalize the first letter of the issue title.
        split_title[0] = split_title[0][0].upper() + split_title[0][1:]

        title = " ".join(split_title)

        formatted_string = (
            f" * {title} [[{self.hash[:11]}](https://projects.blender.org/blender/blender/commit/{self.hash})]"
        )
        if len(self.backport_list) > 0:
            formatted_string += f" - Backported to {' & '.join(self.backport_list)}"
        formatted_string += "\n"

        return formatted_string

    def prepare_for_cache(self) -> tuple[str, dict[str, Any]]:
        return self.hash, {
            'is_revert': self.is_revert,
            'fixed_reports': self.fixed_reports,
            'backport_list': self.backport_list,
            'module': self.module,
            'report_title': self.report_title,
            'classification': self.classification,
        }

    def read_from_cache(self, cache_data: dict[str, Any]) -> None:
        self.is_revert = cache_data['is_revert']
        self.fixed_reports = cache_data['fixed_reports']
        self.backport_list = cache_data['backport_list']
        self.module = cache_data['module']
        self.report_title = cache_data['report_title']
        self.classification = cache_data['classification']

        self.needs_update = False

    def read_from_override(self, override_data: list[str]) -> None:
        self.set_defaults()
        self.fixed_reports = override_data

        self.has_been_overwritten = True

# ---


def setup_commit_info(commit: str) -> CommitInfo | None:
    commit_information = CommitInfo(commit)
    if len(commit_information.fixed_reports) > 0:
        return commit_information
    return None


def get_fix_commits(
        *,
        current_release_tag: str,
        previous_release_tag: str,
        single_thread: bool,
) -> list[CommitInfo]:
    # --no-pager means it prints everything all at once rather than providing a interactive scrollable page.
    # --no-abbrev-commit tells git to always show the full commit hash.
    # -i tells grep to ignore case when searching through commits.
    # -P tells grep to use a specific type of regular expression.

    # This searches for Fix{anything}{one_or_more #}{number}.
    # .* = {anything}
    # #+ = {one_or_more #}
    # \d+ = {number}
    # This captures the common `Fix #123`, but also the less common `Fixes #123`, `Fix for #123`, and `Fix ##123`.
    command = [
        'git',
        '--no-pager',
        'log',
        f'{previous_release_tag}..{current_release_tag}',
        '--oneline',
        '--no-abbrev-commit',
        '-i',
        '-P',
        '--grep',
        r'Fix.*#+\d+',
    ]

    git_log_command_output = subprocess.run(command, capture_output=True).stdout.decode('utf-8')
    git_log_output = git_log_command_output.splitlines()

    if single_thread:
        # Original non-multiprocessing method.
        intial_list_of_commits = []
        for commit in git_log_output:
            intial_list_of_commits.append(setup_commit_info(commit))
    else:
        # Although setup_commit_info is not compute intensive, it is time consuming due to hundreds of git log calls.
        # Multiprocessing can significantly reduce the time taken (E.g. 19s -> 4s on a 32 thread CPU).
        import multiprocessing
        with multiprocessing.Pool() as pool:
            intial_list_of_commits = pool.map(setup_commit_info, git_log_output)

    list_of_commits = [result for result in intial_list_of_commits if result]
    return list_of_commits


# -----------------------------------------------------------------------------
# Utility Functions for `classify_based_on_report()`

def get_version_numbers(broken_lines: str, working_lines: str) -> tuple[list[str], list[str]]:
    def extract_numbers(string: str) -> list[str]:
        return re.findall(r"(\d+\.\d+)", string)

    # Extracts all version numbers from the broken and working fields
    # (Sometimes including weird version numbers like dates).
    temp_broken_versions = extract_numbers(broken_lines)
    temp_working_versions = extract_numbers(working_lines)

    broken_versions = []
    working_versions = []

    for version_number in temp_broken_versions:
        # Filter out any numbers picked up in the previous step that aren't official Blender version numbers.
        if version_number in LIST_OF_OFFICIAL_BLENDER_VERSIONS:
            broken_versions.append(version_number)
    for version_number in temp_working_versions:
        # Filter out any numbers picked up in the previous step that aren't official Blender version numbers.
        if version_number in LIST_OF_OFFICIAL_BLENDER_VERSIONS:
            working_versions.append(version_number)

    return broken_versions, working_versions


def version_extraction(report_body: str) -> tuple[list[str], list[str]]:
    broken_lines = ''
    working_lines = ''
    for line in report_body.splitlines():
        lower_line = line.lower()
        example_in_line = 'example' in lower_line
        if lower_line.startswith('brok') and not example_in_line:
            # Use `brok` to be able to detect different variations of "broken".
            broken_lines += f'{line}\n'
        if lower_line.startswith('work'):
            # Use `work` to be able to detect both "worked" and "working".
            if (not example_in_line) and not ("brok" in lower_line):
                # Don't add the line to the working_lines if it contains the letters "brok"
                # because it means the user probably wrote something like "Worked: It was also broken in X.X"
                # which lead to incorrect information.
                working_lines += f'{line}\n'

    return get_version_numbers(broken_lines, working_lines)


def compare_versions(comparing_version: str, reference_version: str) -> str:
    # Compare two versions of Blender and return how they compare relative to each other.
    comp_version = comparing_version.split(".")
    ref_version = reference_version.split(".")
    comparing_major = int(comp_version[0])
    comparing_minor = int(comp_version[1])
    reference_major = int(ref_version[0])
    reference_minor = int(ref_version[1])

    if comparing_major < reference_major:
        return OLDER_VERION

    if comparing_major == reference_major:
        # The major version matches, so we must compare based on the minor version number.
        if comparing_minor < reference_minor:
            return OLDER_VERION
        if comparing_minor == reference_minor:
            return SAME_VERION

    return NEWER_VERION


# ---

def classify_based_on_report(
        report_body: str,
        *,
        current_version: str,
        previous_version: str,
) -> str:
    if "skip_for_bug_fix_release_notes" in report_body.lower():
        return IGNORED
    # Get a list of broken and working versions of Blender according to the report that was fixed.
    broken_versions, working_versions = version_extraction(report_body)

    broken_is_current_or_newer = False

    for broken_version in broken_versions:
        relative_version = compare_versions(broken_version, current_version)
        if relative_version == OLDER_VERION:
            # Broken version is older than current release. So the issue is from a older version.
            return FIXED_OLD_ISSUE
        if relative_version in (SAME_VERION, NEWER_VERION):
            broken_is_current_or_newer = True

    for working_version in working_versions:
        relative_version = compare_versions(working_version, current_version)
        if relative_version in (SAME_VERION, NEWER_VERION):
            # Working version is current version or newer. So the issue was introduced in this version.
            return FIXED_NEW_ISSUE

    if broken_is_current_or_newer and (previous_version in working_versions):
        # Issue is in current release, but wasn't in previous release.
        # So it must of been introduced in the current release.
        return FIXED_NEW_ISSUE

    return NEEDS_MANUAL_SORTING


# -----------------------------------------------------------------------------
# Utility Functions for `classify_commits()`

def get_backported_commits(issue_number: str) -> dict[str, list[str]]:
    # Adapted from https://projects.blender.org/blender/blender/src/branch/main/release/lts/lts_issue.py

    base_url = "https://projects.blender.org/api/v1/repos"
    issues_url = base_url + "/blender/blender/issues/"

    response = url_json_get(issues_url + issue_number)
    description = response["body"]

    lines = description.split("\n")
    current_version = None

    dict_of_backports: dict[str, list[str]] = {}

    blender_version_start = "## Blender "
    for line in lines:
        if line.startswith(blender_version_start):
            current_version = line.strip(blender_version_start)
        if current_version is None:
            # We haven't got a Blender version yet.
            continue
        if not line.strip():
            continue
        if "|" not in line:
            # Not part of the backports table.
            continue
        if line.startswith("| **Report**"):
            continue
        if line.find("| -- |") != -1:
            continue

        items = line.split("|")
        commit_string = items[2].strip()
        commit_string = commit_string.split(",")[0]
        commit_string = commit_string.split("]")[0]
        commit_string = commit_string.replace("[", "")

        pattern = r"blender/blender@([a-zA-Z0-9]+)"
        matches = re.findall(pattern, commit_string)
        if len(matches) > 0:
            try:
                dict_of_backports[current_version] += matches
            except KeyError:
                dict_of_backports[current_version] = matches

    return dict_of_backports


def get_backports(backport_tasks: list[str]) -> dict[str, list[str]]:
    dict_of_backports: dict[str, list[str]] = {}
    for task in backport_tasks:
        dict_of_backports.update(get_backported_commits(task))

    return dict_of_backports


# ---

def classify_commits(
        backport_tasks: list[str],
        list_of_commits: list[CommitInfo],
        *,
        current_version: str,
        previous_version: str,
) -> None:
    number_of_commits = len(list_of_commits)

    print("Identifying if fixes are for a bug introduced in this release, or if the bug was there in a previous release.")
    print("This requires querying information from Gitea, and can take a while.\n")

    dict_of_backports = get_backports(backport_tasks)

    i = 0
    start_time = time()
    for commit in list_of_commits:
        # Simple progress bar.
        i += 1
        print(
            f"{i}/{number_of_commits} - Estimated time remaining:",
            f"{(((time() - start_time) / i) * (number_of_commits - i)) / 60:.1f} minutes",
            end="\r",
            flush=True
        )

        commit.classify(
            current_version=current_version,
            previous_version=previous_version,
        )
        commit.get_backports(dict_of_backports)

    # Print so we're away from the progress bar.
    print("\n\n\n")


# ---

def prepare_for_print(list_of_commits: list[CommitInfo]) -> dict[str, dict[str, list[CommitInfo]]]:
    # This function takes in a list of commits, and sorts them based on their classification and module.

    dict_of_sorted_commits: dict[str, dict[str, list[CommitInfo]]] = {}
    for item in VALID_CLASSIFICATIONS:
        dict_of_sorted_commits[item] = {}

    for commit in list_of_commits:
        commit_classification = commit.classification
        if commit_classification in VALID_CLASSIFICATIONS:
            commit_module = commit.module
            try:
                # Try to append to a list. If it fails (The list doesn't exist), create the list.
                dict_of_sorted_commits[commit_classification][commit_module].append(commit)
            except KeyError:
                dict_of_sorted_commits[commit_classification][commit_module] = [commit]

    for item in VALID_CLASSIFICATIONS:
        # Sort modules alphabetically
        dict_of_sorted_commits[item] = dict(sorted(dict_of_sorted_commits[item].items()))

    return dict_of_sorted_commits


def print_list_of_commits(title: str, dict_of_commits: dict[str, list[CommitInfo]]) -> None:
    commits_message = ""
    number_of_commits = 0
    unknown_module_commit_message = ""
    for module in dict_of_commits:
        commits_in_this_module = len(dict_of_commits[module])
        number_of_commits += commits_in_this_module
        module_label = f"\n## {module}: {commits_in_this_module}\n"
        module_is_unknown = (module == UNKNOWN)
        if module_is_unknown:
            unknown_module_commit_message += module_label
        else:
            commits_message += module_label

        for commit in dict_of_commits[module]:
            printed_line = commit.generate_release_note_ready_string()

            if module_is_unknown:
                unknown_module_commit_message += printed_line
            else:
                commits_message += printed_line

    if number_of_commits != 0:
        print(f"{title} {number_of_commits}")
        print(commits_message)
        print(unknown_module_commit_message)
        print("\n\n\n")


# ---

def print_release_notes(list_of_commits: list[CommitInfo]) -> None:
    dict_of_sorted_commits = prepare_for_print(list_of_commits)

    print_list_of_commits("Commits that fixed old issues:", dict_of_sorted_commits[FIXED_OLD_ISSUE])

    print_list_of_commits("Revert commits:", dict_of_sorted_commits[REVERT])

    print_list_of_commits("Commits that need manual sorting:", dict_of_sorted_commits[NEEDS_MANUAL_SORTING])

    print_list_of_commits(
        "Commits that need a override (launch this script with -o) as they claim to fix a PR:",
        dict_of_sorted_commits[FIXED_PR])

    print_list_of_commits("Ignored commits:", dict_of_sorted_commits[IGNORED])

    # Currently disabled as this information isn't particularly useful.
    # print_list_of_commits(dict_of_sorted_commits[FIXED_NEW_ISSUE])

    print(r"""What to do with this output:
    - Go through every commit in the "Commits that need manual sorting" section and:
      - Find the corrisponding issue that was fixed (it will be in the commit message)
      - Update the "Broken" and/or "Working" fields of the report with relevant information so this script can sort it.
        - Add a module label if it's missing one.
      - Rerun this script.
    - Repeat the previous steps until there are no commits that need manual sorting.
    - If it is too difficult to track down the broken or working field for a report, then you can add
    `<!-- skip_for_bug_fix_release_notes -->` to the report body and the script will ignore it on subsequent runs.
    - This should be done by the triaging module through out the release cycle, so the list should be quite small.

    - Go through the "Revert commits" section and if needed,
      find the commit they reverted and remove them from the list of "Commits that fixed old issues"
      (This can be done manually or with the overrides feature).
    - Double check if there are any obvious commits in the
      "Commits that fixed old issues" section that shouldn't be there and remove them
      (E.g. A fix for a feature that has been in development over a few releases,
      but was only enabled in this release).
    - Add the output of the "Commits that fixed old issues" section to the release notes:
      https://projects.blender.org/blender/blender-developer-docs/src/branch/main/docs/release_notes
      Here is the release notes for a previous release for reference:
      https://projects.blender.org/blender/blender-developer-docs/src/branch/main/docs/release_notes/4.3/bugfixes.md""")


# -----------------------------------------------------------------------------
# Caching Utilities

def cached_commits_load(list_of_commits: list[CommitInfo]) -> None:
    if PATH_TO_CACHED_COMMITS.exists():
        with open(str(PATH_TO_CACHED_COMMITS), 'r', encoding='utf-8') as file:
            cached_data = json.load(file)
        for commit in list_of_commits:
            if commit.hash in cached_data:
                commit.read_from_cache(cached_data[commit.hash])


def cached_commits_store(list_of_commits: list[CommitInfo]) -> None:
    # Cache information for commits that have been sorted.
    # Commits that still need sorting are not cached.
    # This is done so if a user is repeatably running this script so they can sort
    # the "needs sorting" section, they don't have to wait for information requests to GITEA
    # on commits that are already sorted (and they're not interested in).
    data_to_cache = {}
    for commit in list_of_commits:
        if (commit.classification not in (NEEDS_MANUAL_SORTING, IGNORED)) and not (commit.has_been_overwritten):
            commit_hash, data = commit.prepare_for_cache()
            data_to_cache[commit_hash] = data

    with open(str(PATH_TO_CACHED_COMMITS), 'w', encoding='utf-8') as file:
        json.dump(data_to_cache, file, indent=4)


# -----------------------------------------------------------------------------
# Override Utilities

def overrides_load() -> dict[str, list[str]]:
    override_data = {}
    if PATH_TO_OVERRIDES.exists():
        with open(str(PATH_TO_OVERRIDES), 'r', encoding='utf-8') as file:
            override_data = json.load(file)

    return override_data


def overrides_store(override_data: dict[str, list[str]]) -> None:
    with open(str(PATH_TO_OVERRIDES), 'w', encoding='utf-8') as file:
        json.dump(override_data, file, indent=4)


def overrides_apply(list_of_commits: list[CommitInfo]) -> None:
    override_data = overrides_load()
    if len(override_data) > 0:
        for commit in list_of_commits:
            if commit.hash in override_data:
                commit.read_from_override(override_data[commit.hash])


def create_override() -> None:
    commit_hash = input("Please input the full hash of the commit you want to override: ")
    issue_number = input("Please input the issue number you want to override it with: ")

    override_data = overrides_load()
    override_data[commit_hash] = [issue_number]

    overrides_store(override_data)


# -----------------------------------------------------------------------------
# Argument Parsing

def argparse_create() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        # Don't re-format multi-line text.
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "-o",
        "--override",
        action="store_true",
        help=(
            "Create a override for a commit."
        ),
    )
    parser.add_argument(
        "-st",
        "--single-thread",
        action="store_true",
        help=(
            "Run one of the parts of this script in single threaded mode "
            "(Only really useful for debugging)."
        ),
    )
    parser.add_argument(
        "-c",
        "--cache",
        action="store_true",
        help=(
            "Use caching to speed up re-runs on this script "
            "(IMPORTANT: Leave caching off when collecting the final release notes)."
        ),
    )

    parser.add_argument(
        "-cv",
        "--current-version",
        help=(
            "The common major.minor name of the current version of Blender (E.g. 4.2, 4.3, 4.4)."
        ),
    )
    parser.add_argument(
        "-pv",
        "--previous-version",
        help=(
            "The common major.minor name of the previous version of Blender (E.g. 4.2, 4.3, 4.4)."
        ),
    )

    parser.add_argument(
        "-ct",
        "--current-release-tag",
        help=(
            "The tag for the current release of Blender. "
            "These can be tags (like `v4.3.0`), commit hashes, or branches."
        ),
    )
    parser.add_argument(
        "-pt",
        "--previous-release-tag",
        help=(
            "The tag for the previous release of Blender. "
            "These can be tags (like `v4.3.0`), commit hashes, or branches."
        ))

    parser.add_argument(
        "-bpt",
        "--backport-tasks",
        nargs='+',
        help=(
            "A list of backport tasks. "
            "Backport tasks can be found on the Blender milestones page: "
            "https://projects.blender.org/blender/blender/milestones"
        ),
        default=[],
    )

    parser.add_argument(
        "-s",
        "--silence",
        action="store_true",
        help="Silence some warnings.",
    )

    return parser


def validate_arguments(args: argparse.Namespace) -> bool:
    def print_error(variable_name: str, argument_1: str, argument_2: str) -> None:
        print(f"ERROR: {variable_name} (defined with '{argument_1}' or '{argument_2}') is not defined.")
        print("This script can not proceed without this variable defined.\n")

    should_quit = False
    if args.cache and not args.silence:
        print("WARNING: You are using a cache, this may lead to outdated information on some commits.")
        print("Do not use the cache to generate the final release notes.\n")
    if args.current_version is None:
        print_error("Current version", "-cv", "--current-version")
        should_quit = True
    if args.previous_version is None:
        print_error("Previous version", "-pv", "--previous-version")
        should_quit = True
    if args.current_release_tag is None:
        print_error("Current Release Tag", "-ct", "--current-release-tag")
        should_quit = True
    if args.previous_release_tag is None:
        print_error("Previous Release Tag", "-pt", "--previous-release-tag")
        should_quit = True
    if len(args.backport_tasks) == 0:
        print("WARNING: (Optional) -bpt/--backport-tasks is not defined.")
        if not (args.silence or should_quit):
            yes_no = input("Do you want to proceeed without it? (y/n)")
            if yes_no.lower() == "n":
                should_quit = True

    return not should_quit


# -----------------------------------------------------------------------------
# Main Function


def main() -> int:
    args = argparse_create().parse_args()

    if args.override:
        create_override()
        return 0

    if not validate_arguments(args):
        return 0

    list_of_commits = get_fix_commits(
        current_release_tag=args.current_release_tag,
        previous_release_tag=args.previous_release_tag,
        single_thread=args.single_thread,
    )

    if args.cache:
        cached_commits_load(list_of_commits)

    overrides_apply(list_of_commits)

    classify_commits(
        args.backport_tasks,
        list_of_commits,
        current_version=args.current_version,
        previous_version=args.previous_version,
    )

    if args.cache:
        cached_commits_store(list_of_commits)

    print_release_notes(list_of_commits)
    return 0


if __name__ == "__main__":
    sys.exit(main())

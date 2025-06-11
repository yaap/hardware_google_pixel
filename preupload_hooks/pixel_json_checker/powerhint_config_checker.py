#!/usr/bin/env python3

#
# Copyright 2024, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

"""This program validates pixel powerhint configuration strings
against a predefined list of fields.
"""

from __future__ import print_function

import argparse
import errno
import json
import os
import shutil
import subprocess
import sys
import gitlint.git as git

from pixel_config_checker import PixelJSONFieldNameChecker

def powerhint_check_actions_and_nodes(powerhint_json_files):
  """Preupload check for powerhint actions and nodes.

    This function checks that all powerhint actions on nodes
    and other actions (DoHint, EndHint, etc..) are valid and
    nodes exist. It also validates that values specified in
    actions are supported values in nodes. It also checks if
    nodes are double declared.

    Args: Map of powerhint json file names to actions.

    Returns:
        Status, Error Message.
  """

  for file_path, powerhint in powerhint_json_files.items():
    nodes_dict = dict()
    action_names = set()

    # Create reference Nodes and Actions
    for node in powerhint["Nodes"]:
      if node["Name"] in nodes_dict:
        return False, file_path + ": repeated node " + node["Name"]
      nodes_dict[node["Name"]] = node["Values"]

    for action in powerhint["Actions"]:
      action_names.add(action["PowerHint"])

    for action in powerhint["Actions"]:
      if "Type" in action:
        if action["Value"] not in action_names:
          return False, file_path + ": Action " + action["PowerHint"] + \
            ": unknown Hint " + action["Value"]

      if "Node" in action:
        if action["Node"] not in nodes_dict.keys():
          return False, file_path + ": Action " + action["PowerHint"] + \
            ": unknown Node " + action["Node"]

        if action["Value"] not in nodes_dict[action["Node"]]:
          return False, file_path + ": Action " + action["PowerHint"] + \
            ": Node " + action["Node"] + " unknown value " + action["Value"]

  return True, ""  # Return True if all actions are valid



def get_powerhint_modified_files(commit):
  """Getter for finding which powerhint json files were modified
    in the commit.

    Args: Commit sha

    Returns:
        modified_files: List of powerhint config files modified if any.
  """
  root = git.repository_root()
  modified_files = git.modified_files(root, True, commit)
  modified_files = {f: modified_files[f] for f
                    in modified_files if f.endswith('.json') and 'powerhint' in f}

  return modified_files

def main(args=None):
  """Main function for checking powerhint configs.

    Args:
      commit: The change commit's SHA.
      field_names: The path to known field names.

    Returns:
      Exits with error if unsuccessful.
  """

  # Mapping of form (json path, json object)
  json_files = dict()

  # Load arguments provided from repo hooks.
  parser = argparse.ArgumentParser()
  parser.add_argument('--commit', '-c')
  parser.add_argument('--field_names', '-l')
  args = parser.parse_args()
  if not args.commit:
    return "Invalid commit provided"

  if not args.field_names:
    return "No field names path provided"

  if not git.repository_root():
    return "Not inside a git repository"

  # Gets modified and added json files in current commit.
  powerhint_check_file_paths = get_powerhint_modified_files(args.commit)
  if not list(powerhint_check_file_paths.keys()):
      return 0

  # Populate and validate (json path, json object) maps to test.
  for file_name in powerhint_check_file_paths.keys():
    rel_path = os.path.relpath(file_name)
    content = subprocess.check_output(
        ["git", "show", args.commit + ":" + rel_path])
    try:
        json_file = json.loads(content)
        json_files[rel_path] = json_file
    except ValueError as e:
      return "Malformed JSON file " + rel_path + " with message "+ str(e)

  # Instantiates the common config checker and runs tests on config.
  checker = PixelJSONFieldNameChecker(json_files, args.field_names)
  success, message = checker.check_json_field_names()
  if not success:
    return "powerhint JSON field name check error: " + message

  success, message = powerhint_check_actions_and_nodes(json_files)
  if not success:
    return "powerhint JSON field name check error: " + message

if __name__ == '__main__':
  ret = main()
  if ret:
    print(ret)
    print("----------------------------------------------------")
    print("| !! Please see go/pixel-perf-thermal-preupload !! |")
    print("----------------------------------------------------")
    sys.exit(1)


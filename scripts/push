#!/bin/bash

# Parse command line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
  -m|--message)
      MESSAGE="$2"
      shift 2
      ;;
    *)
      echo "Usage: ./push.sh -m \"commit message\""
      exit 1
      ;;
  esac
done

# Check if message was provided
if [ -z "$MESSAGE" ]; then
  echo "Error: Commit message required"
  echo "Usage: ./push.sh -m \"commit message\""
  exit 1
fi

# Add all changes
echo "Staging changes..."
git add .

# Commit staged changes
echo "Committing changes..."
git commit -m "$MESSAGE"

if [ $? -ne 0 ]; then
  echo "Error: Commit failed"
  exit 1
fi

# Pull latest changes first
echo "Pulling latest changes..."
git pull --rebase origin main

if [ $? -ne 0 ]; then
  echo "Error: Pull failed"
  exit 1
fi

# Push to remote
echo "Pushing to remote..."
git push origin main

if [ $? -eq 0 ]; then
  echo "✓ Successfully pushed to repository"
else
  echo "Error: Push failed"
  exit 1
fi

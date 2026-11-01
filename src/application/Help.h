#pragma once

// writes the user manual (generated from docs/manual.md, compiled into the executable) to the temporary directory and opens it
// in the default web browser; returns false when it could not be written or opened
bool openUserManual();

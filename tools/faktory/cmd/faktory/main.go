// Copyright © 2020 Kontain Inc. All rights reserved.
//
// Kontain Inc CONFIDENTIAL
//
// This file includes unpublished proprietary source code of Kontain Inc. The
// copyright notice above does not evidence any actual or intended publication
// of such source code. Disclosure of this source code or any related
// proprietary information is strictly prohibited without the express written
// permission of Kontain Inc.

package main

import (
	"errors"
	"fmt"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"kontain.app/km/tools/faktory/conversion"
	"kontain.app/km/tools/faktory/splitter"
)

func cmdConvert() *cobra.Command {
	var cmd = &cobra.Command{
		Use:   "convert [from] [to] [using as base]",
		Short: "convert from container to kontain using base",
		Args:  cobra.ExactArgs(3),
		PreRun: func(cmd *cobra.Command, args []string) {
			logrus.WithField("args", args).Debug("convert command is called")
		},
		RunE: func(c *cobra.Command, args []string) error {
			containerName := args[0]
			resultName := args[1]
			baseName := args[2]

			if containerName == "" || resultName == "" || baseName == "" {
				return errors.New("Arguements can't be empty")
			}

			splitter := splitter.PythonSplitter{}
			converter, err := conversion.NewConverter(baseName, splitter)
			if err != nil {
				return err
			}

			if err := converter.Convert(containerName, resultName); err != nil {
				return err
			}

			return nil
		},
	}

	return cmd
}

func main() {
	rootCmd := &cobra.Command{
		Use:          "faktory [commands]",
		Short:        "CLI used to run kontain container",
		SilenceUsage: true,
	}

	rootCmd.PersistentFlags().Bool("debug", false, "Enable debug logging")
	rootCmd.PersistentFlags().String("log-format", "text", "set the format used by logs ('text' or 'json'")

	rootCmd.PersistentPreRunE = func(c *cobra.Command, args []string) error {
		logrus.SetReportCaller(true)

		debug, err := c.Flags().GetBool("debug")
		if err != nil {
			return err
		}

		if debug {
			logrus.SetLevel(logrus.DebugLevel)
		}

		logFormat, err := c.Flags().GetString("log-format")
		if err != nil {
			return err
		}

		switch logFormat {
		case "text":
			// retain the default
		case "json":
			logrus.SetFormatter(new(logrus.JSONFormatter))
		default:
			return fmt.Errorf("Unkown formatter: %s", logFormat)
		}

		return nil
	}

	rootCmd.AddCommand(cmdConvert())

	rootCmd.Execute()
}

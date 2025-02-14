/*
 * Created on Fri Mar 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useRef } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { Descendant, Transforms } from 'slate';
import { Box, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { YBModal, YBSelect } from '../../../../../components';
import { YBEditor } from '../../../../../components/YBEditor';
import { IYBEditor, clearEditor } from '../../../../../components/YBEditor/plugins';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  fetchAlertConfigList,
  previewAlertNotification
} from '../../CustomVariablesAPI';
import { useCommonStyles } from '../../CommonStyles';
import { TextSerializer } from '../../../../../components/YBEditor/serializers/Text/TextSerializer';
import { TextDeserializer } from '../../../../../components/YBEditor/serializers/Text/TextDeSerializer';

type WebhookPreviewModalProps = {
  visible: boolean;
  bodyValue: Descendant[];
  onHide: Function;
};

const useStyles = makeStyles((theme) => ({
  editorRoot: {
    background: 'rgba(217, 217, 217, 0.16)'
  },
  select: {
    width: '488px',
    marginTop: theme.spacing(0.8)
  },
  noPadding: {
    padding: 0
  },
  defaultPadding: {
    padding: `${theme.spacing(2.5)}px ${theme.spacing(3)}px`
  },
  fullHeight: {
    height: '100%'
  },
  bodyEditor: {
    marginTop: theme.spacing(0.8)
  }
}));

const WebhookPreviewModal: FC<WebhookPreviewModalProps> = ({ bodyValue, visible, onHide }) => {
  const bodyEditorRef = useRef<IYBEditor | null>(null);

  const { t } = useTranslation();
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const alertConfigurationsMap = {};

  const { data, isLoading } = useQuery(ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList, () =>
    fetchAlertConfigList({
      uuids: []
    })
  );

  const fillTemplateWithValue = useMutation(
    ({ textTemplate, alertConfigUUID }: { textTemplate: string; alertConfigUUID: string }) =>
      previewAlertNotification(
        {
          type: 'WebHook',
          textTemplate
        },
        alertConfigUUID
      ),
    {
      onSuccess(data) {
        const slateNode = new TextDeserializer(
          bodyEditorRef.current!,
          data.data.text
        ).deserialize();
        clearEditor(bodyEditorRef.current!);
        Transforms.insertNodes(bodyEditorRef.current!, slateNode);
      }
    }
  );

  const previewTemplate = (alertConfigUUID: string) => {
    const textTemplate = new TextSerializer(bodyEditorRef.current!).serlializeElements(bodyValue);
    fillTemplateWithValue.mutate({ textTemplate, alertConfigUUID });
  };

  if (!visible) return null;

  if (isLoading || data?.data === undefined) {
    return <YBLoadingCircleIcon />;
  }

  const alertConfigurations = data.data.sort((a, b) => a.name.localeCompare(b.name));

  alertConfigurations.forEach((alertConfig) => {
    alertConfigurationsMap[alertConfig.uuid] = alertConfig.name;
  });

  return (
    <YBModal
      open={visible}
      title={t('alertCustomTemplates.alertVariablesPreviewModal.modalTitle')}
      dialogContentProps={{ className: clsx(classes.noPadding, commonStyles.noOverflow) }}
      onClose={() => onHide()}
      overrideWidth="740px"
      overrideHeight="540px"
      size="lg"
      titleSeparator
      enableBackdropDismiss
    >
      <Box className={classes.defaultPadding}>
        <Typography variant="body2">
          {t('alertCustomTemplates.alertVariablesPreviewModal.info')}
        </Typography>
        <YBSelect
          className={classes.select}
          data-testid="webhook-preview-select-config"
          onChange={(e) => {
            previewTemplate(e.target.value);
          }}
          renderValue={(selectedAlertConfig) => {
            if (!selectedAlertConfig) {
              return (
                <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
              );
            }
            return alertConfigurationsMap[selectedAlertConfig as string];
          }}
        >
          <MenuItem disabled value="">
            <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
          </MenuItem>
          {Object.keys(alertConfigurationsMap).map((alertConfigUuid) => (
            <MenuItem
              data-testid={`alert-config-${alertConfigurationsMap[alertConfigUuid]}`}
              key={alertConfigurationsMap[alertConfigUuid]}
              value={alertConfigUuid}
            >
              {alertConfigurationsMap[alertConfigUuid]}
            </MenuItem>
          ))}
        </YBSelect>
      </Box>
      <Box className={clsx(classes.editorRoot, classes.defaultPadding, classes.fullHeight)}>
        <Typography variant="body1">{t('alertCustomTemplates.composer.content')}</Typography>
        <Box className={clsx(commonStyles.editorBorder, classes.bodyEditor)}>
          <YBEditor
            editorProps={{
              readOnly: true,
              style: { height: '260px' },
              'data-testid': 'preview-webhook-subject-editor'
            }}
            loadPlugins={{ alertVariablesPlugin: false, jsonPlugin: true }}
            initialValue={bodyValue}
            ref={bodyEditorRef}
          />
        </Box>
      </Box>
    </YBModal>
  );
};

export default WebhookPreviewModal;

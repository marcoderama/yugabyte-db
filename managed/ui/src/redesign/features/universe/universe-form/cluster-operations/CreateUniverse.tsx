import React, { FC, useContext } from 'react';
import { useSelector } from 'react-redux';
import { useTranslation } from 'react-i18next';
import { browserHistory } from 'react-router';
import { UniverseForm } from '../UniverseForm';
import { ClusterType, ClusterModes, DEFAULT_FORM_DATA, UniverseFormData } from '../utils/dto';
import { UniverseFormContext } from '../UniverseFormContainer';
import { createUniverse, filterFormDataByClusterType, getAsyncCopyFields } from '../utils/helpers';
import { useUpdateEffect, useEffectOnce } from 'react-use';

interface CreateUniverseProps {}

export const CreateUniverse: FC<CreateUniverseProps> = () => {
  const { t } = useTranslation();
  const [contextState, contextMethods] = useContext(UniverseFormContext);
  const { isLoading, primaryFormData, asyncFormData, clusterType } = contextState;
  const {
    initializeForm,
    toggleClusterType,
    setPrimaryFormData,
    setAsyncFormData,
    setLoader
  } = contextMethods;
  const featureFlags = useSelector((state: any) => state.featureFlags);
  const isPrimary = clusterType === ClusterType.PRIMARY;

  useEffectOnce(() => {
    initializeForm({
      clusterType: ClusterType.PRIMARY,
      mode: ClusterModes.CREATE,
      newUniverse: true
    });
  });

  useUpdateEffect(() => {
    toggleClusterType(ClusterType.ASYNC);
  }, [primaryFormData]);

  useUpdateEffect(() => {
    toggleClusterType(ClusterType.PRIMARY);
  }, [asyncFormData]);

  useUpdateEffect(() => {
    setLoader(false);
  }, [clusterType]);

  const onSubmit = (primaryData: UniverseFormData, asyncData: UniverseFormData) => {
    createUniverse({ primaryData, asyncData, universeContextData: contextState, featureFlags });
  };

  const onCancel = () => {
    browserHistory.goBack();
  };

  if (isLoading) return <>Loading .... </>;

  if (isPrimary)
    return (
      <UniverseForm
        defaultFormData={primaryFormData ?? DEFAULT_FORM_DATA}
        title={t('universeForm.createUniverse')}
        onFormSubmit={(data: UniverseFormData) =>
          onSubmit(
            data,
            asyncFormData ? { ...asyncFormData, ...getAsyncCopyFields(primaryFormData) } : null
          )
        }
        onCancel={onCancel}
        onClusterTypeChange={(data: UniverseFormData) => {
          setLoader(true);
          setPrimaryFormData(data);
        }}
        key={ClusterType.PRIMARY}
      />
    );
  else
    return (
      <UniverseForm
        defaultFormData={
          asyncFormData
            ? { ...asyncFormData, ...getAsyncCopyFields(primaryFormData) } //Not all the fields needs to be copied from primary -> async
            : filterFormDataByClusterType(primaryFormData, ClusterType.ASYNC)
        }
        title={t('universeForm.configReadReplica')}
        onFormSubmit={(data: UniverseFormData) => onSubmit(primaryFormData, data)}
        onCancel={onCancel}
        onClusterTypeChange={(data: UniverseFormData) => {
          setLoader(true);
          setAsyncFormData(data);
        }}
        key={ClusterType.ASYNC}
      />
    );
};
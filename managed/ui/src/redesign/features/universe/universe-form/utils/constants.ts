// ------------Universe Form Fields Path Start------------

//Cloud config
export const UNIVERSE_NAME_FIELD = 'cloudConfig.universeName';
export const PROVIDER_FIELD = 'cloudConfig.provider';
export const REGIONS_FIELD = 'cloudConfig.regionList';
export const REPLICATION_FACTOR_FIELD = 'cloudConfig.replicationFactor';
export const AUTO_PLACEMENT_FIELD = 'cloudConfig.autoPlacement';
export const TOTAL_NODES_FIELD = 'cloudConfig.numNodes';
export const PLACEMENTS_FIELD = 'cloudConfig.placements';

//Instance config
export const INSTANCE_TYPE_FIELD = 'instanceConfig.instanceType';
export const DEVICE_INFO_FIELD = 'instanceConfig.deviceInfo';
export const ASSIGN_PUBLIC_IP_FIELD = 'instanceConfig.assignPublicIP';
export const YSQL_FIELD = 'instanceConfig.enableYSQL';
export const YSQL_AUTH_FIELD = 'instanceConfig.enableYSQLAuth';
export const YSQL_PASSWORD_FIELD = 'instanceConfig.ysqlPassword';
export const YSQL_CONFIRM_PASSWORD_FIELD = 'instanceConfig.ysqlConfirmPassword';
export const YCQL_FIELD = 'instanceConfig.enableYCQL';
export const YCQL_AUTH_FIELD = 'instanceConfig.enableYCQLAuth';
export const YCQL_PASSWORD_FIELD = 'instanceConfig.ycqlPassword';
export const YCQL_CONFIRM_PASSWORD_FIELD = 'instanceConfig.ycqlConfirmPassword';
export const YEDIS_FIELD = 'instanceConfig.enableYEDIS';
export const TIME_SYNC_FIELD = 'instanceConfig.useTimeSync';
export const CLIENT_TO_NODE_ENCRYPT_FIELD = 'instanceConfig.enableClientToNodeEncrypt';
export const ROOT_CERT_FIELD = 'instanceConfig.rootCA';
export const NODE_TO_NODE_ENCRYPT_FIELD = 'instanceConfig.enableNodeToNodeEncrypt';
export const EAR_FIELD = 'instanceConfig.enableEncryptionAtRest';
export const KMS_CONFIG_FIELD = 'instanceConfig.kmsConfig';

//Advanced config
export const SYSTEMD_FIELD = 'advancedConfig.useSystemd';
export const YBC_PACKAGE_PATH_FIELD = 'advancedConfig.ybcPackagePath';
export const AWS_ARN_STRING_FIELD = 'advancedConfig.awsArnString';
export const IPV6_FIELD = 'advancedConfig.enableIPV6';
export const EXPOSING_SERVICE_FIELD = 'advancedConfig.enableExposingService';
export const CUSTOMIZE_PORT_FIELD = 'advancedConfig.customizePort';
export const ACCESS_KEY_FIELD = 'advancedConfig.accessKeyCode';
export const SOFTWARE_VERSION_FIELD = 'advancedConfig.ybSoftwareVersion';
export const COMMUNICATION_PORTS_FIELD = 'advancedConfig.communicationPorts';

//Gflags
export const GFLAGS_FIELD = 'gFlags';

//Tags
export const USER_TAGS_FIELD = 'instanceTags';

// ------------Universe Form Fields Path End------------

//Other form related constants
export const MIN_PLACEMENTS_FOR_GEO_REDUNDANCY = 3;

export const PRIMARY_FIELDS = [
  UNIVERSE_NAME_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  AUTO_PLACEMENT_FIELD,
  TOTAL_NODES_FIELD,
  PLACEMENTS_FIELD,
  INSTANCE_TYPE_FIELD,
  GFLAGS_FIELD,
  USER_TAGS_FIELD,
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YSQL_PASSWORD_FIELD,
  YSQL_CONFIRM_PASSWORD_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  YCQL_PASSWORD_FIELD,
  YCQL_CONFIRM_PASSWORD_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  ROOT_CERT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  EAR_FIELD,
  KMS_CONFIG_FIELD,
  AWS_ARN_STRING_FIELD,
  COMMUNICATION_PORTS_FIELD,
  ACCESS_KEY_FIELD,
  CUSTOMIZE_PORT_FIELD
];

export const ASYNC_FIELDS = [
  UNIVERSE_NAME_FIELD,
  PROVIDER_FIELD,
  REGIONS_FIELD,
  REPLICATION_FACTOR_FIELD,
  AUTO_PLACEMENT_FIELD,
  TOTAL_NODES_FIELD,
  PLACEMENTS_FIELD,
  INSTANCE_TYPE_FIELD,
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD,
  ROOT_CERT_FIELD,
  EAR_FIELD
];

export const ASYNC_COPY_FIELDS = [
  SOFTWARE_VERSION_FIELD,
  DEVICE_INFO_FIELD,
  ASSIGN_PUBLIC_IP_FIELD,
  SYSTEMD_FIELD,
  TIME_SYNC_FIELD,
  YSQL_FIELD,
  YSQL_AUTH_FIELD,
  YCQL_FIELD,
  YCQL_AUTH_FIELD,
  IPV6_FIELD,
  EXPOSING_SERVICE_FIELD,
  YEDIS_FIELD,
  NODE_TO_NODE_ENCRYPT_FIELD,
  CLIENT_TO_NODE_ENCRYPT_FIELD,
  ACCESS_KEY_FIELD,
  ROOT_CERT_FIELD,
  EAR_FIELD
];
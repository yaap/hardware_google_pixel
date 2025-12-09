ifeq ($(USES_ALCEDO_MODEM), true)
# Pixel Logger for Alcedo devices
PRODUCT_PACKAGES_DEBUG += PixelLoggerNext
else
# Pixel Logger for Qualcomm and Lassen devices
PRODUCT_PACKAGES_DEBUG += PixelLogger
BOARD_SEPOLICY_DIRS += hardware/google/pixel-sepolicy/logger_app
endif

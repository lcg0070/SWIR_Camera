import cv2
import numpy as np


paths = ['AC.png', 'PC.png', 'PET.png', 'PVC.png']
names = ['AC', 'PC', 'PET', 'PVC']

averages = {}
center_values = {}


def compute_central_average_cv(image, region_size=10):
    h, w = image.shape
    center_y, center_x = int(h / 2), int(w / 2)
    half = int(region_size / 2)

    region = image[
             center_y - half: center_y + half,
             center_x - half: center_x + half
             ]
    return np.mean(region)

for name, path in zip(names, paths):
    img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)

    if img is None:
        print(f"Error: {path} could not be loaded.")
        continue

    center_pixel = img[img.shape[0] // 2, img.shape[1] // 2]
    region_avg = compute_central_average_cv(img)

    center_values[name] = center_pixel
    averages[name] = region_avg


print("Center Brightness Values:")
for name in names:
    print(f"{name}: {averages[name]:.2f}")
